#include "app/AppController.hpp"
#include <chrono>
#include <iostream>

namespace MidiWire {

static std::string sanitizeWebSocketUrl(std::string url) {
    url.erase(0, url.find_first_not_of(" \t\r\n"));
    url.erase(url.find_last_not_of(" \t\r\n") + 1);

    if (url.rfind("ss://", 0) == 0 || url.rfind("s://", 0) == 0) {
        url = "w" + url;
    }

    if (url.find("onrender.com") != std::string::npos || url.rfind("https://", 0) == 0) {
        if (url.rfind("https://", 0) == 0) {
            url.replace(0, 8, "wss://");
        } else if (url.rfind("ws://", 0) == 0) {
            url.replace(0, 5, "wss://");
        } else if (url.rfind("wss://", 0) != 0) {
            url = "wss://" + url;
        }
    } else if (url.rfind("http://", 0) == 0) {
        url.replace(0, 7, "ws://");
    } else if (url.rfind("ws://", 0) != 0 && url.rfind("wss://", 0) != 0) {
        url = "ws://" + url;
    }
    return url;
}

AppController::AppController(std::unique_ptr<IMidiDriver> midiDriver)
    : midiEngine_(std::make_unique<MidiEngine>(std::move(midiDriver))),
      netEngine_(std::make_unique<NetworkEngine>()),
      signalingClient_(std::make_unique<SignalingClient>()),
      jitterBuffer_(15) {
}

AppController::~AppController() {
    stop();
    panic();
}

bool AppController::initialize() {
    if (!midiEngine_ || !netEngine_ || !signalingClient_) {
        return false;
    }

    if (!midiEngine_->initialize()) {
        std::cerr << "[AppController] Failed to initialize MidiEngine.\n";
        return false;
    }

    netEngine_->initialize("stun:stun.l.google.com:19302");

    // Inbound Remote MIDI Callback with Timestamp Safeguard
    netEngine_->setOnMidiReceived([this](const std::vector<uint8_t>& payload) {
        MidiPacket pkt = Serializer::deserialize(payload);
        
        // Ignore status noise
        if (pkt.status == 0xF8 || pkt.status == 0xFE) return;

        uint64_t nowNs = ClockSynchronizer::getCurrentMonotonicNs();
        uint64_t convertedNs = clockSync_.remoteToLocalTime(pkt.timestampNs);

        // Fallback to immediate playback if clock sync offset is uncalibrated/unreasonable
        if (convertedNs == 0 || convertedNs > nowNs + 5000000000ULL || convertedNs < nowNs - 5000000000ULL) {
            pkt.timestampNs = nowNs + (static_cast<uint64_t>(jitterBuffer_.getBufferDepthMs()) * 1000000ULL);
        } else {
            pkt.timestampNs = convertedNs + (static_cast<uint64_t>(jitterBuffer_.getBufferDepthMs()) * 1000000ULL);
        }

        jitterBuffer_.push(pkt);
    });

    // WebRTC Local Offer/Candidate signaling binding
    netEngine_->setOnLocalDescription([this](const std::string& type, const std::string& sdp) {
        if (signalingClient_) {
            std::cout << "[NET] Sending Local " << type << " SDP to signaling server..." << std::endl;
            signalingClient_->sendSignal(type, {{"sdp", sdp}});
        }
    });

    netEngine_->setOnLocalCandidate([this](const std::string& candidate, const std::string& mid) {
        if (signalingClient_) {
            signalingClient_->sendSignal("candidate", {{"candidate", candidate}, {"mid", mid}});
        }
    });

    // Inbound Signaling Event Handler
    // Inbound Signaling Message Handler
    signalingClient_->setOnSignal([this](const std::string& type, const nlohmann::json& payload) {
        if (!netEngine_) return;

        // 1. Peer Discovery: ONLY the existing peer initiates an offer when a new peer joins
        if (type == "peer_joined" || type == "user_joined" || type == "peer-joined") {
            std::cout << "[NET] Remote peer joined room. Initiating WebRTC Offer..." << std::endl;
            netEngine_->createOffer();
            return;
        }

        // 2. Extract SDP payload
        nlohmann::json data = payload;
        if (payload.contains("data") && payload["data"].is_object()) data = payload["data"];
        if (payload.contains("payload") && payload["payload"].is_object()) data = payload["payload"];

        std::string sdpType = type;
        if (data.contains("type") && data["type"].is_string()) {
            sdpType = data["type"].get<std::string>();
        }

        // 3. Process Remote Description (Offers generate Answers automatically inside NetworkEngine)
        if (sdpType == "offer" || sdpType == "answer") {
            std::cout << "[NET] Processing remote " << sdpType << " SDP..." << std::endl;
            std::string sdp;
            if (data.contains("sdp") && data["sdp"].is_string()) {
                sdp = data["sdp"].get<std::string>();
            }
            netEngine_->handleRemoteDescription(sdpType, sdp);
        } 
        // 4. ICE Candidates
        else if (sdpType == "candidate" || sdpType == "ice_candidate" || type == "candidate") {
            std::string candidate;
            std::string mid;
            if (data.contains("candidate") && data["candidate"].is_string()) {
                candidate = data["candidate"].get<std::string>();
            }
            if (data.contains("mid") && data["mid"].is_string()) {
                mid = data["mid"].get<std::string>();
            }
            netEngine_->handleRemoteCandidate(candidate, mid);
        }
    });

    return true;
}

void AppController::connectToSignaling(const std::string& signalingUrl) {
    if (signalingClient_) {
        std::string cleanUrl = sanitizeWebSocketUrl(signalingUrl);
        signalingClient_->connect(cleanUrl);
    }
}

void AppController::joinRoom(const std::string& roomId) {
    currentRoomId_ = roomId;
    if (signalingClient_) {
        signalingClient_->joinRoom(roomId);
    }
}

std::vector<std::string> AppController::getAvailableMidiDevices() {
    if (midiEngine_) {
        return midiEngine_->refreshInputDevices();
    }
    return {};
}

bool AppController::selectMidiInputDevice(uint32_t index) {
    if (!midiEngine_) {
        return false;
    }
    midiEngine_->stopInputDevice();
    return midiEngine_->startInputDevice(index, [this](const MidiPacket& packet) {
        handleLocalMidiInput(packet);
    });
}

bool AppController::setupVirtualMidiPort(const std::string& portName) {
    if (midiEngine_) {
        return midiEngine_->setupVirtualPort(portName);
    }
    return false;
}

void AppController::setJitterBufferDepthMs(uint32_t ms) {
    jitterBuffer_.setBufferDepthMs(ms);
}

void AppController::panic() {
    if (midiEngine_) {
        midiEngine_->panic();
    }
    jitterBuffer_.clear();
}

void AppController::start() {
    if (running_.load()) {
        return;
    }
    running_.store(true);
    playbackThread_ = std::thread(&AppController::playbackLoop, this);
}

void AppController::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    if (playbackThread_.joinable()) {
        playbackThread_.join();
    }
}

void AppController::handleLocalMidiInput(const MidiPacket& packet) {
    // Drop continuous clock noise
    if (packet.status == 0xF8 || packet.status == 0xFE) {
        return;
    }

    if (netEngine_ && netEngine_->isConnected()) {
        MidiPacket pkt = packet;
        if (pkt.timestampNs == 0) {
            pkt.timestampNs = ClockSynchronizer::getCurrentMonotonicNs();
        }
        std::vector<uint8_t> bytes = Serializer::serialize(pkt);
        netEngine_->sendMidi(bytes);
    }
}

void AppController::onLocalMidiPacket(const MidiPacket& packet) {
    handleLocalMidiInput(packet);
}

void AppController::playbackLoop() {
    while (running_.load()) {
        uint64_t nowNs = ClockSynchronizer::getCurrentMonotonicNs();
        MidiPacket readyPkt;
        while (jitterBuffer_.popReadyPacket(nowNs, readyPkt)) {
            if (midiEngine_) {
                midiEngine_->sendToVirtualPort(readyPkt);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace MidiWire