#include "inbox.h"

#include <algorithm>
#include <cstdlib>

namespace tg {

using facet::Json;

namespace {

// TDLib sends int53 as JSON numbers and int64 as strings.
int64_t id_of(const Json& j) {
    if (j.is_string()) return std::strtoll(j.str().c_str(), nullptr, 10);
    return int64_t(j.as_number());
}

const std::string& type_of(const Json& j) { return j["@type"].str(); }

}  // namespace

Preview describe(const Json& content) {
    const std::string& t = type_of(content);
    const std::string& caption = content["caption"]["text"].str();
    if (t == "messageText") return {"Message", content["text"]["text"].str()};
    if (t == "messagePhoto") return {"Photo", caption};
    if (t == "messageVideo") return {"Video", caption};
    if (t == "messageAnimation") return {"GIF", caption};
    if (t == "messageVoiceNote") return {"Voice message", caption};
    if (t == "messageVideoNote") return {"Video message", ""};
    if (t == "messageAudio") return {"Audio", caption};
    if (t == "messageDocument") {
        const std::string& name = content["document"]["file_name"].str();
        return {"File", caption.empty() ? name : caption};
    }
    if (t == "messageSticker") return {"Sticker", content["sticker"]["emoji"].str()};
    if (t == "messageLocation" || t == "messageVenue") return {"Location", ""};
    if (t == "messageContact") return {"Contact", ""};
    if (t == "messagePoll") return {"Poll", content["poll"]["question"]["text"].str()};
    if (t == "messageCall") return {content["is_video"].as_bool() ? "Video call" : "Call", ""};
    if (t == "messageDice") return {"Dice", content["emoji"].str()};
    if (t == "messageStory") return {"Story", ""};
    if (t == "messageGift") return {"Gift", content["text"]["text"].str()};
    if (t == "messageContactRegistered") return {"Joined Telegram", ""};
    return {"Message", ""};
}

void Inbox::clear() { *this = Inbox(); }

void Inbox::set_notifications(Chat& c, const Json& s) {
    c.default_mute = s["use_default_mute_for"].as_bool(true);
    c.mute_for = s["mute_for"].as_int(0);
    c.default_preview = s["use_default_show_preview"].as_bool(true);
    c.show_preview = s["show_preview"].as_bool(true);
}

void Inbox::set_last(Chat& c, const Json& m) {
    c.has_last = m.is_object();
    if (!c.has_last) return;
    c.last.id = id_of(m["id"]);
    c.last.date = id_of(m["date"]);
    c.last.incoming = !m["is_outgoing"].as_bool();
    c.last.preview = describe(m["content"]);
}

bool Inbox::apply(const Json& u) {
    const std::string& t = type_of(u);
    if (t == "updateNewChat") {
        const Json& j = u["chat"];
        Chat& c = chats_[id_of(j["id"])];
        c.id = id_of(j["id"]);
        c.title = j["title"].str();
        c.is_private = type_of(j["type"]) == "chatTypePrivate";
        c.user_id = c.is_private ? id_of(j["type"]["user_id"]) : 0;
        c.unread = j["unread_count"].as_int();
        c.marked_unread = j["is_marked_as_unread"].as_bool();
        c.color = j["accent_color_id"].as_int();
        set_notifications(c, j["notification_settings"]);
        set_last(c, j["last_message"]);
        return true;
    }
    if (t == "updateUser") {
        const Json& j = u["user"];
        users_[id_of(j["id"])].bot = type_of(j["type"]) == "userTypeBot";
        return true;
    }
    if (t == "updateOption") {
        if (u["name"].str() == "my_id") my_id_ = id_of(u["value"]["value"]);
        return false;
    }
    if (t == "updateScopeNotificationSettings") {
        if (type_of(u["scope"]) != "notificationSettingsScopePrivateChats") return false;
        scope_mute_for_ = u["notification_settings"]["mute_for"].as_int();
        scope_show_preview_ = u["notification_settings"]["show_preview"].as_bool(true);
        return true;
    }

    // The rest refers to a known chat.
    auto it = chats_.find(id_of(u["chat_id"]));
    if (it == chats_.end()) return false;
    Chat& c = it->second;
    if (t == "updateChatTitle") c.title = u["title"].str();
    else if (t == "updateChatLastMessage") set_last(c, u["last_message"]);
    else if (t == "updateChatReadInbox") c.unread = u["unread_count"].as_int();
    else if (t == "updateChatIsMarkedAsUnread") c.marked_unread = u["is_marked_as_unread"].as_bool();
    else if (t == "updateChatNotificationSettings") set_notifications(c, u["notification_settings"]);
    else if (t == "updateChatAccentColors") c.color = u["accent_color_id"].as_int();
    else return false;
    return true;
}

std::vector<Entry> Inbox::entries(const Filters& f) const {
    std::vector<Entry> out;
    for (const auto& [id, c] : chats_) {
        if (!c.is_private || c.user_id == my_id_) continue;  // groups, channels, Saved Messages
        if (c.unread <= 0 && !c.marked_unread) continue;
        bool muted = (c.default_mute ? scope_mute_for_ : c.mute_for) > 0;
        if (f.hide_muted && muted) continue;
        auto user = users_.find(c.user_id);
        if (!f.include_bots && user != users_.end() && user->second.bot) continue;
        Entry e;
        e.chat_id = id;
        e.title = c.title;
        e.unread = std::max(0, c.unread);
        e.date = c.has_last ? c.last.date : 0;
        e.has_preview = c.has_last && c.last.incoming;
        e.preview = c.last.preview;
        e.hide_text = !(c.default_preview ? scope_show_preview_ : c.show_preview) || c.user_id == kServiceUserId;
        e.color = c.color;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        return a.date != b.date ? a.date > b.date : a.chat_id < b.chat_id;
    });
    return out;
}

}  // namespace tg
