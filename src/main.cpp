// telegram-dm-notif: shows unread messages from private Telegram chats on the panel.
// Signs in as a regular Telegram client through TDLib (like Telegram Desktop)
// and only reads: nothing is sent and nothing is marked as read.
// Runs as a Facet plugin process (see facet-core docs).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "facet/plugin.h"
#include "i18n/i18n.h"
#include "inbox.h"
#include "qrcodegen.hpp"
#include "td_client.h"

using facet::Json;
using facet::sdk::Canvas;
using facet::sdk::Plugin;
using facet::sdk::Screen;

namespace {

#ifndef TG_VERSION
#define TG_VERSION "dev"  // set by CMake
#endif

constexpr size_t kMaxShown = 30;

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// First code point of each of the first two words: "Anna Petrova" -> "AP".
std::string initials(const std::string& title) {
    std::string out;
    bool word_start = true;
    int taken = 0;
    for (size_t i = 0; i < title.size() && taken < 2;) {
        unsigned char c = static_cast<unsigned char>(title[i]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (c == ' ') {
            word_start = true;
        } else if (word_start) {
            bool letter = len > 1 || std::isalnum(c);
            if (letter) {
                std::string cp = title.substr(i, len);
                if (len == 1) cp[0] = char(std::toupper(c));
                out += cp;
                ++taken;
            }
            word_start = false;
        }
        i += len;
    }
    return out.empty() ? "?" : out;
}

std::string one_line(std::string s) {
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}

// Telegram's avatar palette by accent colour id.
const char* avatar_color(int id) {
    static const char* kColors[] = {"#E17076", "#F5A35B", "#A695E7", "#7BC862", "#6EC9CB", "#65AADD", "#EE7AAE"};
    return kColors[(id % 7 + 7) % 7];
}

struct Settings {
    int api_id = 0;
    std::string api_hash;
    bool show_text = true;
    bool hide_muted = true;
    bool include_bots = false;

    bool has_api() const { return api_id > 0 && api_hash.size() >= 16; }

    Json to_json() const {
        Json j = Json::object();
        j["api_id"] = api_id;
        j["api_hash"] = api_hash;
        j["show_text"] = show_text;
        j["hide_muted"] = hide_muted;
        j["include_bots"] = include_bots;
        return j;
    }
    static Settings from_json(const Json& j) {
        Settings s;
        s.api_id = j["api_id"].as_int(0);
        s.api_hash = j["api_hash"].as_string();
        s.show_text = j["show_text"].as_bool(true);
        s.hide_muted = j["hide_muted"].as_bool(true);
        s.include_bots = j["include_bots"].as_bool(false);
        return s;
    }
};

class App {
public:
    explicit App(Plugin& plugin) : plugin_(plugin) {}

    void start() {
        Json j;
        if (facet::load_json_file(settings_path(), j)) settings_ = Settings::from_json(j);
        api_id_input_ = settings_.api_id ? std::to_string(settings_.api_id) : "";
        api_hash_input_ = settings_.api_hash;
        Json verbosity = Json::object();
        verbosity["@type"] = "setLogVerbosityLevel";
        verbosity["new_verbosity_level"] = 1;
        tg::TdClient::execute(verbosity);
        td_ = std::make_unique<tg::TdClient>();
        refresh();
    }

    // Closes TDLib (it flushes its database) and waits up to 3 s for it.
    void stop() {
        if (!td_ || stopping_) return;
        stopping_ = true;
        send("close");
        for (int i = 0; i < 60 && auth_ != "authorizationStateClosed"; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            poll();
        }
    }

    void poll() {
        if (!td_) return;
        std::deque<Json> msgs;
        td_->drain(msgs);
        for (const Json& m : msgs) handle(m);
        if (signout_armed_ > 0 && now_s() > signout_armed_) signout_armed_ = 0, dirty_ = true;
        if (dirty_) refresh();
    }

    void on_event(const std::string& id, const Json& v) {
        if (id == "api_id") api_id_input_ = v.str();
        else if (id == "api_hash") api_hash_input_ = v.str();
        else if (id == "phone") phone_input_ = v.str();
        else if (id == "code") code_input_ = v.str();
        else if (id == "password") password_input_ = v.str();
        else if (id == "save_api") save_api();
        else if (id == "qr") request("requestQrCodeAuthentication", "qr");
        else if (id == "use_phone") phone_form_ = true;
        else if (id == "send_phone") send_phone();
        else if (id == "send_code") send_code();
        else if (id == "send_password") send_password();
        else if (id == "restart") start_over();
        else if (id == "change_api") change_api();
        else if (id == "sign_out") sign_out();
        else if (id == "show_text") settings_.show_text = v.as_bool(), save();
        else if (id == "hide_muted") settings_.hide_muted = v.as_bool(), save();
        else if (id == "include_bots") settings_.include_bots = v.as_bool(), save();
        refresh();
    }

    void on_submit(const std::string& id, const std::string& text) {
        on_event(id, Json(text));
        if (id == "api_hash") save_api();
        else if (id == "phone") send_phone();
        else if (id == "code") send_code();
        else if (id == "password") send_password();
        refresh();
    }

    void refresh() {
        dirty_ = false;
        plugin_.set_tile(tile());
        update_notifications();
        if (plugin_.visible()) plugin_.set_ui(build());
    }

private:
    // One system notification per chat with new messages (permission
    // "notifications", optional), withdrawn once the chat is read anywhere;
    // the tile badge shows the unread count. Messages that were already
    // unread when we signed in are not announced again.
    void update_notifications() {
        bool ready = settings_.has_api() && auth_ == "authorizationStateReady";
        auto list = ready ? entries() : std::vector<tg::Entry>();
        int unread = 0;
        for (const auto& e : list) unread += std::max(1, e.unread);
        plugin_.set_badge(unread);
        if (!plugin_.has_permission("notifications")) return;

        std::map<int64_t, int64_t> still;
        for (const auto& e : list) {
            auto old = notified_.find(e.chat_id);
            bool fresh = e.has_preview && e.date >= ready_since_ - 5 &&
                         (old == notified_.end() || old->second != e.message_id);
            if (fresh && !plugin_.visible()) {
                facet::sdk::Notification n;
                n.id = "chat:" + std::to_string(e.chat_id);
                n.title = e.title;
                n.body = preview_line(e);
                if (e.unread > 1) n.body = tr("{} new · {}", {std::to_string(e.unread), n.body});
                n.priority = "high";
                plugin_.notify(n);
            }
            still[e.chat_id] = old != notified_.end() && !fresh ? old->second : e.message_id;
        }
        for (const auto& [chat, msg] : notified_)
            if (!still.count(chat)) plugin_.cancel_notification("chat:" + std::to_string(chat));
        notified_ = std::move(still);
    }

    std::string tr(std::string_view key) const { return plugin_.tr(key); }
    std::string tr(std::string_view key, const std::vector<std::string>& args) const { return plugin_.tr(key, args); }
    std::string settings_path() const { return plugin_.data_dir() + "/settings.json"; }

    void save() {
        if (!facet::save_json_file(settings_path(), settings_.to_json()))
            Plugin::log("cannot save settings to %s", settings_path().c_str());
    }

    void send(const char* type, Json req = Json::object(), const std::string& extra = {}) {
        req["@type"] = type;
        td_->send(std::move(req), extra);
    }
    void request(const char* type, const std::string& extra) {
        error_.clear();
        Json req = Json::object();
        if (std::string(type) == "requestQrCodeAuthentication") req["other_user_ids"] = Json::array();
        send(type, std::move(req), extra);
        busy_ = true;
    }

    void send_parameters() {
        if (!settings_.has_api() || params_sent_) return;
        const std::string db = plugin_.data_dir() + "/tdlib";
        Json p = Json::object();
        p["use_test_dc"] = false;
        p["database_directory"] = db;
        p["files_directory"] = db + "/files";
        p["database_encryption_key"] = "";
        p["use_file_database"] = false;
        p["use_chat_info_database"] = true;
        p["use_message_database"] = true;
        p["use_secret_chats"] = false;
        p["api_id"] = settings_.api_id;
        p["api_hash"] = settings_.api_hash;
        p["system_language_code"] = plugin_.catalog().language();
        p["device_model"] = "Facet panel";
        p["system_version"] = "Linux";
        p["application_version"] = TG_VERSION;
        send("setTdlibParameters", std::move(p), "params");
        params_sent_ = true;
    }

    void save_api() {
        int id = std::atoi(api_id_input_.c_str());
        std::string hash = api_hash_input_;
        hash.erase(std::remove(hash.begin(), hash.end(), ' '), hash.end());
        if (id <= 0 || hash.size() != 32) {
            error_ = tr("Check the API ID (a number) and the API hash (32 characters).");
            return;
        }
        error_.clear();
        settings_.api_id = id;
        settings_.api_hash = hash;
        save();
        if (auth_ == "authorizationStateWaitTdlibParameters") send_parameters();
        else if (!auth_.empty()) start_over();  // parameters of a running instance cannot change
    }

    void send_phone() {
        std::string digits;
        for (char c : phone_input_)
            if (c >= '0' && c <= '9') digits += c;
        if (digits.size() < 7) {
            error_ = tr("Enter the full number with the country code.");
            return;
        }
        Json req = Json::object();
        req["phone_number"] = "+" + digits;
        error_.clear();
        send("setAuthenticationPhoneNumber", std::move(req), "phone");
        busy_ = true;
    }

    void send_code() {
        if (code_input_.empty()) return;
        Json req = Json::object();
        req["code"] = code_input_;
        error_.clear();
        send("checkAuthenticationCode", std::move(req), "code");
        busy_ = true;
    }

    void send_password() {
        if (password_input_.empty()) return;
        Json req = Json::object();
        req["password"] = password_input_;
        password_input_.clear();  // kept only until it is handed to TDLib
        error_.clear();
        send("checkAuthenticationPassword", std::move(req), "password");
        busy_ = true;
    }

    // Drops an unfinished sign-in: closes the instance; a new one starts at
    // the beginning (a finished sign-in would survive, it is in the database).
    void start_over() {
        error_.clear();
        phone_form_ = false;
        code_input_.clear();
        password_input_.clear();
        if (auth_ == "authorizationStateClosed") restart_client();
        else send("close");
    }

    void change_api() {
        settings_.api_id = 0;
        settings_.api_hash.clear();
        save();
        start_over();
    }

    void sign_out() {
        if (signout_armed_ == 0) {
            signout_armed_ = now_s() + 5;  // second tap within 5 s confirms
            return;
        }
        signout_armed_ = 0;
        send("logOut");
    }

    void restart_client() {
        inbox_.clear();
        me_.clear();
        params_sent_ = false;
        td_->restart();
    }

    void handle(const Json& m) {
        const std::string& type = m["@type"].str();
        const std::string& extra = m["@extra"].str();
        if (type == "updateAuthorizationState") {
            const Json& st = m["authorization_state"];
            auth_ = st["@type"].str();
            auth_state_ = st;
            busy_ = false;
            if (auth_ != "authorizationStateWaitOtherDeviceConfirmation" && auth_ != "authorizationStateWaitPhoneNumber")
                phone_form_ = false;
            if (auth_ == "authorizationStateWaitTdlibParameters") {
                params_sent_ = false;
                send_parameters();
            } else if (auth_ == "authorizationStateReady") {
                code_input_.clear();
                ready_since_ = int64_t(std::time(nullptr));
                Json load = Json::object();
                load["chat_list"]["@type"] = "chatListMain";
                load["limit"] = 200;
                send("loadChats", std::move(load), "load");
                send("getMe", Json::object(), "me");
            } else if (auth_ == "authorizationStateClosed" && !stopping_) {
                restart_client();
            }
            dirty_ = true;
        } else if (type == "updateConnectionState") {
            connection_ = m["state"]["@type"].str();
            dirty_ = true;
        } else if (type == "error") {
            if (extra == "load" || extra.empty()) return;  // "all chats loaded" and the like
            busy_ = false;
            error_ = explain(m["message"].str());
            dirty_ = true;
        } else if (type == "user" && extra == "me") {
            me_ = one_line(m["first_name"].str() + " " + m["last_name"].str());
            dirty_ = true;
        } else if (inbox_.apply(m)) {
            dirty_ = true;
        }
    }

    // TDLib error messages are English error codes; the common ones get a
    // readable, translated text.
    std::string explain(const std::string& code) const {
        if (code == "PHONE_NUMBER_INVALID") return tr("This phone number is not valid.");
        if (code == "PHONE_CODE_INVALID") return tr("Wrong code.");
        if (code == "PHONE_CODE_EXPIRED") return tr("The code has expired. Start over.");
        if (code == "PASSWORD_HASH_INVALID") return tr("Wrong password.");
        if (code == "API_ID_INVALID" || code == "API_ID_PUBLISHED_FLOOD")
            return tr("Telegram rejected the API ID. Check it on my.telegram.org.");
        if (code.rfind("Too Many Requests", 0) == 0 || code.rfind("FLOOD_WAIT", 0) == 0)
            return tr("Too many attempts. Try again later.");
        return code;
    }

    std::string tile() const {
        if (!settings_.has_api() || auth_ != "authorizationStateReady") return tr("Not signed in");
        int total = 0;
        auto list = entries();
        for (const auto& e : list) total += std::max(1, e.unread);
        if (total == 0) return tr("No new messages");
        return tr("New: {} · {}", {std::to_string(total), list.front().title});
    }

    std::vector<tg::Entry> entries() const {
        return inbox_.entries({settings_.hide_muted, settings_.include_bots});
    }

    std::string when(int64_t date) const {
        if (date <= 0) return {};
        std::time_t t = std::time_t(date), now = std::time(nullptr);
        std::tm tm{}, today{};
        localtime_r(&t, &tm);
        localtime_r(&now, &today);
        char buf[16];
        if (tm.tm_year == today.tm_year && tm.tm_yday == today.tm_yday) std::strftime(buf, sizeof buf, "%H:%M", &tm);
        else std::strftime(buf, sizeof buf, "%d.%m", &tm);
        return buf;
    }

    std::string preview_line(const tg::Entry& e) const {
        if (!e.has_preview) return tr("You replied last");
        const tg::Preview& p = e.preview;
        if (!settings_.show_text || e.hide_text || p.text.empty())
            return p.kind == "Message" ? tr("New message") : tr(p.kind);
        if (p.kind == "Message") return one_line(p.text);
        return tr(p.kind) + ": " + one_line(p.text);
    }

    // --- Screens -----------------------------------------------------------

    Canvas message_list(const std::vector<tg::Entry>& list, float w, float& height) const {
        Canvas c;
        const float row = 80, gap = 10, r = 26;
        size_t n = std::min(list.size(), kMaxShown);
        height = float(n) * (row + gap) - gap;
        for (size_t i = 0; i < n; ++i) {
            const tg::Entry& e = list[i];
            float y = float(i) * (row + gap);
            c.rrect(0, y, w, row, 16, "surface");
            c.circle(16 + r, y + row / 2, r, avatar_color(e.color));
            c.text(16, y + row / 2 - r, 2 * r, 2 * r, initials(e.title), 19, "#FFFFFF", "center", "medium");
            float tx = 16 + 2 * r + 14, time_w = 70;
            c.text(tx, y + 12, w - tx - time_w - 16, 28, e.title, 18, "text", "start", "medium");
            c.text(w - 16 - time_w, y + 12, time_w, 28, when(e.date), 14, "text_dim", "end");
            float badge_w = 0;
            if (e.unread > 0) {
                std::string count = e.unread > 999 ? "999+" : std::to_string(e.unread);
                badge_w = std::max(26.f, 14.f + 9.f * float(count.size()));
                c.rrect(w - 16 - badge_w, y + 44, badge_w, 24, 12, "accent");
                c.text(w - 16 - badge_w, y + 44, badge_w, 24, count, 14, "on_accent", "center", "medium");
            }
            c.text(tx, y + 42, w - tx - badge_w - 28, 26, preview_line(e), 15, "text_dim");
        }
        return c;
    }

    // QR code with a quiet zone, dark modules on white as scanners expect.
    Canvas qr_code(const std::string& link, float w, float& height) const {
        Canvas c;
        auto qr = qrcodegen::QrCode::encodeText(link.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        int n = qr.getSize();
        float side = std::min(w, 340.f), cell = side / float(n + 8), x0 = (w - side) / 2;
        height = side;
        c.rrect(x0, 0, side, side, 12, "#FFFFFF");
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n;) {
                if (!qr.getModule(x, y)) {
                    ++x;
                    continue;
                }
                int run = x;
                while (run < n && qr.getModule(run, y)) ++run;
                // Slight overlap hides hairline seams between neighbouring rows.
                c.rect(x0 + cell * float(x + 4), cell * float(y + 4), cell * float(run - x), cell + 0.3f, "#000000");
                x = run;
            }
        }
        return c;
    }

    void error_note(Screen& ui) const {
        if (!error_.empty()) ui.note(tr("Error") + ": " + error_);  // a note wraps long messages
    }

    void phone_form(Screen& ui) const {
        ui.text_field("phone", tr("Phone number"), phone_input_, tr("country code and number"), false, "number", 20);
        ui.button("send_phone", tr("Send code"), "primary");
    }

    Screen build() const {
        Screen ui("Telegram");
        const float w = float(plugin_.content_width());
        float h = 0;

        if (!settings_.has_api()) {
            ui.section(tr("Connect Telegram"));
            ui.note(tr("Facet signs in to Telegram as a regular client app, like Telegram Desktop. Telegram "
                       "asks every such app for its own API ID. Open my.telegram.org on a phone or computer, "
                       "sign in, choose “API development tools”, create an app with any name and enter its "
                       "api_id and api_hash here. They stay on this device."));
            ui.text_field("api_id", tr("API ID"), api_id_input_, "1234567", false, "number", 12);
            ui.text_field("api_hash", tr("API hash"), api_hash_input_, "0123456789abcdef…", false, "text", 40);
            error_note(ui);
            ui.button("save_api", tr("Continue"), "primary");
            return ui;
        }

        const std::string& a = auth_;
        if (a == "authorizationStateReady") {
            if (connection_ != "connectionStateReady" && !connection_.empty()) {
                std::string state = connection_ == "connectionStateWaitingForNetwork" ? tr("waiting for network")
                                    : connection_ == "connectionStateUpdating"        ? tr("updating…")
                                                                                      : tr("connecting…");
                ui.info(tr("Connection"), state, "warn");
            }
            auto list = entries();
            ui.section(tr("New messages"));
            if (list.empty()) {
                ui.note(tr("No unread messages in private chats."));
            } else {
                Canvas c = message_list(list, w, h);
                ui.canvas("messages", h, c);
                if (list.size() > kMaxShown) ui.note(tr("And {} more chats.", {std::to_string(list.size() - kMaxShown)}));
            }

            ui.section(tr("Settings"));
            ui.toggle("show_text", tr("Show message text"), settings_.show_text);
            ui.toggle("hide_muted", tr("Hide muted chats"), settings_.hide_muted);
            ui.toggle("include_bots", tr("Include bots"), settings_.include_bots);

            ui.section(tr("Account"));
            if (!me_.empty()) ui.info(tr("Signed in as"), me_);
            ui.button("sign_out", signout_armed_ > 0 ? tr("Tap again to sign out") : tr("Sign out"),
                      signout_armed_ > 0 ? "danger" : "normal");
            ui.note(tr("Read only: Facet never sends messages and never marks them as read. A chat disappears "
                       "from this list once you read it in any Telegram app."));
            return ui;
        }

        ui.section(tr("Sign in"));
        if (a.empty() || a == "authorizationStateWaitTdlibParameters") {
            ui.info(tr("Telegram"), tr("starting…"), "dim");
        } else if (a == "authorizationStateWaitPhoneNumber") {
            ui.note(tr("The quickest way: scan a QR code with Telegram on your phone."));
            ui.button("qr", tr("Sign in with a QR code"), "primary");
            phone_form(ui);
            ui.button("change_api", tr("Change API ID"));
        } else if (a == "authorizationStateWaitOtherDeviceConfirmation") {
            Canvas c = qr_code(auth_state_["link"].str(), w, h);
            ui.canvas("qr", h, c);
            ui.note(tr("On your phone open Telegram > Settings > Devices > Link Desktop Device and point the "
                       "camera at this code."));
            if (phone_form_) phone_form(ui);
            else ui.button("use_phone", tr("Use the phone number instead"));
        } else if (a == "authorizationStateWaitCode") {
            const std::string& via = auth_state_["code_info"]["type"]["@type"].str();
            ui.note(via == "authenticationCodeTypeTelegramMessage" ? tr("Telegram sent a code to your other devices.")
                    : via == "authenticationCodeTypeSms"            ? tr("A code was sent by SMS.")
                                                                    : tr("Telegram sent you a code."));
            ui.text_field("code", tr("Code"), code_input_, "12345", false, "number", 12);
            ui.button("send_code", tr("Sign in"), "primary");
            ui.button("restart", tr("Start over"));
        } else if (a == "authorizationStateWaitPassword") {
            ui.note(tr("The account has two-step verification. Enter its password."));
            const std::string& hint = auth_state_["password_hint"].str();
            if (!hint.empty()) ui.info(tr("Hint"), hint);
            ui.text_field("password", tr("Password"), password_input_, "", true);
            ui.button("send_password", tr("Sign in"), "primary");
            ui.button("restart", tr("Start over"));
        } else if (a == "authorizationStateWaitRegistration") {
            ui.note(tr("There is no Telegram account with this number. Create it in the Telegram app first."));
            ui.button("restart", tr("Start over"));
        } else if (a == "authorizationStateLoggingOut" || a == "authorizationStateClosing" ||
                   a == "authorizationStateClosed") {
            ui.info(tr("Telegram"), tr("signing out…"), "dim");
        } else {
            // E-mail setup, Premium purchase and other steps only the official apps can do.
            ui.note(tr("Telegram asks for a step Facet cannot do. Sign in once in the official app, then try "
                       "again."));
            ui.button("restart", tr("Start over"));
        }
        if (busy_) ui.info(tr("Telegram"), tr("please wait…"), "dim");
        error_note(ui);
        return ui;
    }

    Plugin& plugin_;
    std::unique_ptr<tg::TdClient> td_;
    Settings settings_;
    tg::Inbox inbox_;
    std::string auth_, connection_, error_, me_;
    Json auth_state_;
    bool params_sent_ = false, busy_ = false, phone_form_ = false, dirty_ = false, stopping_ = false;
    double signout_armed_ = 0;
    int64_t ready_since_ = 0;              // unix time of the sign-in (or start)
    std::map<int64_t, int64_t> notified_;  // chat -> last message id announced or known
    // Sign-in form values; the password is dropped as soon as it is sent.
    std::string api_id_input_, api_hash_input_, phone_input_, code_input_, password_input_;
};

}  // namespace

int main() {
    Plugin plugin("telegram-dm-notif", TG_VERSION);
    tg::register_translations(plugin.catalog());
    App app(plugin);
    plugin.on_hello = [&](const Json&) { app.start(); };
    plugin.on_event = [&](const std::string& id, const Json& v) { app.on_event(id, v); };
    plugin.on_submit = [&](const std::string& id, const std::string& text) { app.on_submit(id, text); };
    plugin.on_visible = [&](bool) { app.refresh(); };
    plugin.on_locale = [&](const std::string&) { app.refresh(); };
    plugin.on_layout = [&](int) { app.refresh(); };
    plugin.on_tick = [&] { app.poll(); };
    plugin.on_shutdown = [&] { app.stop(); };
    int rc = plugin.run(250);
    app.stop();  // also when the core just closed our stdin
    // TDLib's global client manager must not be destroyed by exit handlers
    // while its threads still run (it aborts): leave without them.
    std::fflush(nullptr);
    std::_Exit(rc);
}
