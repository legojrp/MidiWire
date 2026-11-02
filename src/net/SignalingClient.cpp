#include "net/SignalingClient.hpp"
#include <iostream>

namespace MidiWire {

SignalingClient::SignalingClient() = default;

SignalingClient::~SignalingClient() {
    disconnect();
}

void SignalingClient::setOnConnected(ConnectedCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onConnected_ = cb;
}

void SignalingClient::setOnDisconnected(DisconnectedCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onDisconnected_ = cb;
}

void SignalingClient::setOnSignal(SignalCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onSignal_ = cb;
}

void SignalingClient::connect(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ws_ = std::make_shared<rtc::WebSocket>();

    ws_->onOpen([this]() {
        std::cout << "[SignalingClient] Connected to signaling server successfully.\n";
        
        ConnectedCallback connCb;
        std::string roomId;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            connCb = onConnected_;
            roomId = currentRoomId_;
        }

        if (connCb) connCb();

        // Auto-send room join message as soon as socket connects
        if (!roomId.empty() && ws_ && ws_->isOpen()) {
            try {
                nlohmann::json msg = {{"type", "join"}, {"roomId", roomId}};
                ws_->send(msg.dump());
                std::cout << "[SignalingClient] Joined room: " << roomId << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[SignalingClient] Exception during auto-join: " << e.what() << "\n";
            }
        }
    });

    ws_->onClosed([this]() {
        std::cout << "[SignalingClient] Disconnected from signaling server.\n";
        DisconnectedCallback cb;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            cb = onDisconnected_;
        }
        if (cb) cb();
    });

    ws_->onError([](const std::string& error) {
        std::cerr << "[SignalingClient] WebSocket error: " << error << "\n";
    });

    ws_->onMessage([this](rtc::message_variant data) {
        if (std::holds_alternative<std::string>(data)) {
            try {
                auto jsonMsg = nlohmann::json::parse(std::get<std::string>(data));
                std::string type = jsonMsg.value("type", "");
                nlohmann::json payload = jsonMsg.value("payload", nlohmann::json::object());

                SignalCallback cb;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    cb = onSignal_;
                }
                if (cb) cb(type, payload);
            } catch (const std::exception& e) {
                std::cerr << "[SignalingClient] Error parsing JSON: " << e.what() << "\n";
            }
        }
    });

    try {
        ws_->open(url);
    } catch (const std::exception& e) {
        std::cerr << "[SignalingClient] Failed to open WebSocket URL: " << e.what() << "\n";
    }
}

void SignalingClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ws_) {
        ws_->onOpen(nullptr);
        ws_->onClosed(nullptr);
        ws_->onError(nullptr);
        ws_->onMessage(nullptr);
        ws_->close();
        ws_.reset();
    }
}

void SignalingClient::joinRoom(const std::string& roomId) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentRoomId_ = roomId;

    if (ws_ && ws_->isOpen()) {
        try {
            nlohmann::json msg = {{"type", "join"}, {"roomId", roomId}};
            ws_->send(msg.dump());
            std::cout << "[SignalingClient] Sent join request for room: " << roomId << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[SignalingClient] Exception joining room: " << e.what() << "\n";
        }
    }
}

void SignalingClient::sendSignal(const std::string& type, const nlohmann::json& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ws_ && ws_->isOpen()) {
        try {
            nlohmann::json msg = {{"type", type}, {"payload", payload}};
            ws_->send(msg.dump());
        } catch (const std::exception& e) {
            std::cerr << "[SignalingClient] Exception sending signal: " << e.what() << "\n";
        }
    } else {
        std::cerr << "[SignalingClient] Warning: Cannot send signal '" << type << "', socket is not open.\n";
    }
}

} // namespace MidiWire