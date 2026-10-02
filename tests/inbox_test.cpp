// Inbox fed with TDLib-shaped JSON updates.
#include <cstdio>
#include <string>

#include "inbox.h"

using facet::Json;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

Json parse(const std::string& text) {
    Json j;
    if (!Json::parse(text, j)) std::printf("bad test JSON: %s\n", text.c_str());
    return j;
}

std::string text_message(long long id, long long chat, bool outgoing, const std::string& text, long long date) {
    return R"({"@type":"message","id":)" + std::to_string(id) + R"(,"chat_id":)" + std::to_string(chat) +
           R"(,"is_outgoing":)" + (outgoing ? "true" : "false") + R"(,"date":)" + std::to_string(date) +
           R"(,"content":{"@type":"messageText","text":{"@type":"formattedText","text":")" + text + R"("}}})";
}

std::string new_chat(long long id, const std::string& type, const std::string& title, int unread,
                     const std::string& last, bool muted = false) {
    return R"({"@type":"updateNewChat","chat":{"@type":"chat","id":)" + std::to_string(id) + R"(,"type":)" + type +
           R"(,"title":")" + title + R"(","unread_count":)" + std::to_string(unread) +
           R"(,"is_marked_as_unread":false,"accent_color_id":3,"last_message":)" + last +
           R"(,"notification_settings":{"use_default_mute_for":)" + (muted ? "false" : "true") +
           R"(,"mute_for":)" + (muted ? "2147483647" : "0") + R"(,"use_default_show_preview":true,"show_preview":true}}})";
}

std::string priv(long long user) { return R"({"@type":"chatTypePrivate","user_id":)" + std::to_string(user) + "}"; }

}  // namespace

int main() {
    tg::Inbox inbox;
    inbox.apply(parse(R"({"@type":"updateOption","name":"my_id","value":{"@type":"optionValueInteger","value":"100"}})"));
    check(inbox.my_id() == 100, "my_id from an int64 string option");

    inbox.apply(parse(new_chat(1, priv(1), "Anna", 2, text_message(10, 1, false, "hi there", 5000))));
    inbox.apply(parse(new_chat(2, priv(2), "Bob", 1, text_message(20, 2, false, "later", 6000))));
    inbox.apply(parse(new_chat(3, priv(3), "Muted Max", 4, text_message(30, 3, false, "x", 7000), true)));
    inbox.apply(parse(new_chat(-1004, R"({"@type":"chatTypeSupergroup","supergroup_id":4,"is_channel":false})",
                               "Group", 9, text_message(40, -1004, false, "g", 8000))));
    inbox.apply(parse(new_chat(100, priv(100), "Saved", 1, "null")));
    inbox.apply(parse(new_chat(5, priv(5), "Some Bot", 1, text_message(50, 5, false, "/start", 4000))));
    inbox.apply(parse(R"({"@type":"updateUser","user":{"@type":"user","id":5,"type":{"@type":"userTypeBot"}}})"));
    inbox.apply(parse(new_chat(777000, priv(777000), "Telegram", 1, text_message(60, 777000, false, "Login code: 12345", 3000))));

    auto e = inbox.entries({});
    check(e.size() == 3, "private, unmuted, non-bot, not self");
    check(e.size() == 3 && e[0].title == "Bob" && e[1].title == "Anna" && e[2].title == "Telegram", "newest first");
    check(!e.empty() && e[1].unread == 2 && e[1].preview.text == "hi there" && e[1].has_preview, "preview");
    check(e.size() == 3 && e[2].hide_text, "service notifications never show their text");
    check(inbox.entries({false, true}).size() == 5, "muted and bots shown when the filters are off");

    // Read on another device: the chat leaves the list.
    inbox.apply(parse(R"({"@type":"updateChatReadInbox","chat_id":2,"last_read_inbox_message_id":20,"unread_count":0})"));
    e = inbox.entries({});
    check(e.size() == 2 && e[0].title == "Anna", "read chats disappear");

    // A new message in a chat with previews disabled by the scope.
    inbox.apply(parse(R"({"@type":"updateScopeNotificationSettings","scope":{"@type":"notificationSettingsScopePrivateChats"},"notification_settings":{"mute_for":0,"show_preview":false}})"));
    inbox.apply(parse(R"({"@type":"updateChatLastMessage","chat_id":1,"last_message":{"@type":"message","id":11,"is_outgoing":false,"date":9000,"content":{"@type":"messagePhoto","caption":{"text":"sunset"}}},"positions":[]})"));
    inbox.apply(parse(R"({"@type":"updateChatReadInbox","chat_id":1,"last_read_inbox_message_id":9,"unread_count":3})"));
    e = inbox.entries({});
    check(!e.empty() && e[0].title == "Anna" && e[0].unread == 3, "unread count updated");
    check(!e.empty() && e[0].preview.kind == "Photo" && e[0].preview.text == "sunset", "photo caption");
    check(!e.empty() && e[0].hide_text, "scope hides previews");

    // Muting everything through the scope hides chats that use the default.
    inbox.apply(parse(R"({"@type":"updateScopeNotificationSettings","scope":{"@type":"notificationSettingsScopePrivateChats"},"notification_settings":{"mute_for":100,"show_preview":true}})"));
    check(inbox.entries({}).empty(), "scope mute applies to chats with default settings");

    if (failures) return 1;
    std::printf("ok: inbox tests passed\n");
    return 0;
}
