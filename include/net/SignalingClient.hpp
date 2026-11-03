#pragma once

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace MidiWire {

class SignalingClient {
public:
    using ConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void()>;
    using SignalCallback = std::function<void(const std::string& type, const nlohmann::json& payload)>;

    SignalingClient();
    ~SignalingClient();

    void setOnConnected(ConnectedCallback cb);
    void setOnDisconnected(DisconnectedCallback cb);
    void setOnSignal(SignalCallback cb);

    void connect(const std::string& url);
    void disconnect();
    void joinRoom(const std::string& roomId);
    void sendSignal(const std::string& type, const nlohmann::json& payload);

private:
    void sendJoinRoomInternal(const std::string& roomId);
    
    std::string pendingRoomId_;
    std::shared_ptr<rtc::WebSocket> ws_;
    ConnectedCallback onConnected_;
    DisconnectedCallback onDisconnected_;
    SignalCallback onSignal_;
    std::string currentRoomId_;
    mutable std::mutex mutex_;
};

} // namespace MidiWire