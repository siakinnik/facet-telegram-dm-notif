#include "td_client.h"

#include <td/telegram/td_json_client.h>

namespace tg {

using facet::Json;

namespace {

// A new instance stays silent until it gets its first request; this one
// makes it start and report its authorization state.
void wake(int client_id) { td_send(client_id, R"({"@type":"getOption","name":"version"})"); }

}  // namespace

TdClient::TdClient() {
    client_id_ = td_create_client_id();
    thread_ = std::thread([this] { receive_loop(); });
    wake(client_id_);
}

TdClient::~TdClient() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void TdClient::restart() {
    client_id_ = td_create_client_id();
    wake(client_id_);
}

void TdClient::send(Json request, const std::string& extra) {
    if (!extra.empty()) request["@extra"] = extra;
    td_send(client_id_, request.dump().c_str());
}

void TdClient::drain(std::deque<Json>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        out.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
}

Json TdClient::execute(const Json& request) {
    const char* answer = td_execute(request.dump().c_str());
    Json out;
    if (answer) Json::parse(answer, out);
    return out;
}

void TdClient::receive_loop() {
    // td_receive() serves all instances; it must only be called from one thread.
    while (running_) {
        const char* text = td_receive(0.5);
        if (!text) continue;
        Json msg;
        if (!Json::parse(text, msg)) continue;
        if (int(msg["@client_id"].as_number()) != client_id_) continue;  // a closed instance
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(msg));
    }
}

}  // namespace tg
