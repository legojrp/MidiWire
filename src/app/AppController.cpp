#include "app/AppController.hpp"

#include <chrono>

namespace MidiWire {

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

    // b. Inbound Remote MIDI Callback
    netEngine_->setOnMidiReceived([this](const std::vector<uint8_t>& payload) {
        MidiPacket pkt = Serializer::deserialize(payload);
        uint64_t targetLocalNs = clockSync_.remoteToLocalTime(pkt.timestampNs) 
                                + (static_cast<uint64_t>(jitterBuffer_.getBufferDepthMs()) * 1000000ULL);
        pkt.timestampNs = targetLocalNs;
        jitterBuffer_.push(pkt);
    });

    // c. WebRTC Local Offer/Candidate signaling binding
    netEngine_->setOnLocalDescription([this](const std::string& type, const std::string& sdp) {
        if (signalingClient_) {
            signalingClient_->sendSignal(type, {{"sdp", sdp}});
        }
    });

    netEngine_->setOnLocalCandidate([this](const std::string& candidate, const std::string& mid) {
        if (signalingClient_) {
            signalingClient_->sendSignal("candidate", {{"candidate", candidate}, {"mid", mid}});
        }
    });

    // d. Inbound Signaling Callback
    signalingClient_->setOnSignal([this](const std::string& type, const nlohmann::json& payload) {
        if (!netEngine_) return;

        if (type == "offer" || type == "answer") {
            std::string sdp;
            if (payload.contains("sdp") && payload["sdp"].is_string()) {
                sdp = payload["sdp"].get<std::string>();
            }
            netEngine_->handleRemoteDescription(type, sdp);
            if (type == "offer") {
                netEngine_->createOffer();
            }
        } else if (type == "candidate") {
            std::string candidate;
            std::string mid;
            if (payload.contains("candidate") && payload["candidate"].is_string()) {
                candidate = payload["candidate"].get<std::string>();
            }
            if (payload.contains("mid") && payload["mid"].is_string()) {
                mid = payload["mid"].get<std::string>();
            }
            netEngine_->handleRemoteCandidate(candidate, mid);
        }
    });

    return true;
}

void AppController::connectToSignaling(const std::string& signalingUrl) {
    if (signalingClient_) {
        signalingClient_->connect(signalingUrl);
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
    MidiPacket pkt = packet;
    if (pkt.timestampNs == 0) {
        pkt.timestampNs = ClockSynchronizer::getCurrentMonotonicNs();
    }
    std::vector<uint8_t> bytes = Serializer::serialize(pkt);
    if (netEngine_) {
        netEngine_->sendMidi(bytes);
    }
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

void AppController::onLocalMidiPacket(const MidiPacket& packet) {
    // Drop continuous MIDI clock pulses (0xF8)
    if (packet.status == 0xF8) {
        return;
    }

    // Forward to WebRTC if connected
    if (netEngine_ && netEngine_->isConnected()) {
        auto payload = Serializer::serialize(packet);
        netEngine_->sendMidi(payload);
    }
}
} // namespace MidiWire