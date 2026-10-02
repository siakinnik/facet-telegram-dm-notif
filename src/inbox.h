// Unread private chats built from TDLib updates. Pure logic: fed with parsed
// TDLib JSON objects, no I/O, so it is unit tested without Telegram.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "facet/json.h"

namespace tg {

// What a message is, for the preview line: `kind` is an English UI string
// ("Photo", "Voice message", ...), `text` the message text or caption.
struct Preview {
    std::string kind;
    std::string text;
};

Preview describe(const facet::Json& content);

struct Filters {
    bool hide_muted = true;
    bool include_bots = false;
};

struct Entry {
    int64_t chat_id = 0;
    std::string title;
    int unread = 0;       // 0 for chats only marked as unread
    int64_t date = 0;     // unix time of the last message
    Preview preview;      // of the last message, if it is incoming
    bool has_preview = false;
    bool hide_text = false;  // Telegram hides previews for this chat, or a service chat
    int color = 0;        // accent colour id for the avatar
};

class Inbox {
public:
    // Applies one TDLib update; returns true if it may change entries().
    bool apply(const facet::Json& update);

    std::vector<Entry> entries(const Filters& f) const;
    int64_t my_id() const { return my_id_; }
    void clear();

private:
    struct Message {
        int64_t id = 0, date = 0;
        bool incoming = false;
        Preview preview;
    };
    struct Chat {
        int64_t id = 0, user_id = 0;
        bool is_private = false;
        std::string title;
        int unread = 0;
        bool marked_unread = false;
        bool has_last = false;
        Message last;
        bool default_mute = true, default_preview = true, show_preview = true;
        int mute_for = 0;
        int color = 0;
    };
    struct User {
        bool bot = false;
    };

    void set_notifications(Chat& c, const facet::Json& settings);
    void set_last(Chat& c, const facet::Json& message);

    std::map<int64_t, Chat> chats_;
    std::map<int64_t, User> users_;
    int64_t my_id_ = 0;
    int scope_mute_for_ = 0;
    bool scope_show_preview_ = true;
};

// Telegram's own account that sends login codes: never shown as text.
constexpr int64_t kServiceUserId = 777000;

}  // namespace tg
