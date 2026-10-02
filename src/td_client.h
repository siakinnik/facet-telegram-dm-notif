// Thin wrapper around TDLib's JSON interface: one TDLib client, a receiver
// thread and a queue the plugin thread drains on every tick.
#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "facet/json.h"

namespace tg {

class TdClient {
public:
    TdClient();
    ~TdClient();
    TdClient(const TdClient&) = delete;
    TdClient& operator=(const TdClient&) = delete;

    // Creates a fresh TDLib instance (after the previous one reported
    // authorizationStateClosed). Updates of the old one are dropped.
    void restart();
    // Sends a request; `extra` comes back as "@extra" in its answer.
    void send(facet::Json request, const std::string& extra = {});
    // Moves everything received so far into `out`.
    void drain(std::deque<facet::Json>& out);

    // Synchronous TDLib calls that need no instance (e.g. log settings).
    static facet::Json execute(const facet::Json& request);

private:
    void receive_loop();

    std::atomic<int> client_id_{0};
    std::atomic<bool> running_{true};
    std::mutex mutex_;
    std::deque<facet::Json> queue_;
    std::thread thread_;
};

}  // namespace tg
