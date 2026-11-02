#include "net/NetworkEngine.hpp"
#include <iostream>
#include <algorithm>

namespace MidiWire {

NetworkEngine::NetworkEngine() = default;

NetworkEngine::~NetworkEngine() {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> midiChan;
    std::shared_ptr<rtc::DataChannel> ctrlChan;

    // Extract pointers and release class references under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pc = pc_;
        midiChan = midiChannel_;
        ctrlChan = controlChannel_;

        pc_.reset();
        midiChannel_.reset();
        controlChannel_.reset();
        onMidiReceived_ = nullptr;
        onControlReceived_ = nullptr;
        onStateChanged_ = nullptr;
        onLocalDescription_ = nullptr;
        onLocalCandidate_ = nullptr;
    }

    // Clear callbacks and close connections OUTSIDE lock to prevent deadlock
    if (pc) {
        pc->onStateChange(nullptr);
        pc->onLocalDescription(nullptr);
        pc->onLocalCandidate(nullptr);
        pc->onDataChannel(nullptr);
        pc->close();
    }
    if (midiChan) {
        midiChan->onMessage(nullptr);
        midiChan->close();
    }
    if (ctrlChan) {
        ctrlChan->onMessage(nullptr);
        ctrlChan->close();
    }
}

void NetworkEngine::initialize(const std::string& stunServer) {
    std::lock_guard<std::mutex> lock(mutex_);

    rtc::Configuration config;
    if (!stunServer.empty()) {
        config.iceServers.emplace_back(stunServer);
    }

    pc_ = std::make_shared<rtc::PeerConnection>(config);

    pc_->onLocalDescription([this](rtc::Description desc) {
        DescriptionCallback cb;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            cb = onLocalDescription_;
        }
        if (cb) {
            cb(desc.typeString(), std::string(desc));
        }
    });

    pc_->onLocalCandidate([this](rtc::Candidate cand) {
        CandidateCallback cb;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            cb = onLocalCandidate_;
        }
        if (cb) {
            cb(cand.candidate(), cand.mid());
        }
    });

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        StateCallback cb;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            cb = onStateChanged_;
        }
        if (cb) {
            cb(state);
        }
    });

    pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> channel) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!channel) return;
        if (channel->label() == "midi-fast") {
            midiChannel_ = channel;
            setupDataChannelCallbacks(midiChannel_, true);
        } else if (channel->label() == "midi-control") {
            controlChannel_ = channel;
            setupDataChannelCallbacks(controlChannel_, false);
        }
    });
}

void NetworkEngine::createOffer() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pc_) return;

    rtc::DataChannelInit fastConfig;
    fastConfig.reliability.maxRetransmits = 0;
    fastConfig.reliability.unordered = true;
    midiChannel_ = pc_->createDataChannel("midi-fast", fastConfig);
    setupDataChannelCallbacks(midiChannel_, true);

    rtc::DataChannelInit controlConfig;
    controlChannel_ = pc_->createDataChannel("midi-control", controlConfig);
    setupDataChannelCallbacks(controlChannel_, false);

    pc_->setLocalDescription(rtc::Description::Type::Offer);
}

void NetworkEngine::handleRemoteDescription(const std::string& type, const std::string& sdp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pc_) return;

    rtc::Description desc(sdp, type);
    pc_->setRemoteDescription(desc);
}

void NetworkEngine::handleRemoteCandidate(const std::string& candidate, const std::string& mid) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pc_) return;

    rtc::Candidate cand(candidate, mid);
    pc_->addRemoteCandidate(cand);
}

void NetworkEngine::setOnMidiReceived(BinaryCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onMidiReceived_ = cb;
}

void NetworkEngine::setOnControlReceived(BinaryCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onControlReceived_ = cb;
}

void NetworkEngine::setOnStateChanged(StateCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onStateChanged_ = cb;
}

void NetworkEngine::setOnLocalDescription(DescriptionCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onLocalDescription_ = cb;
}

void NetworkEngine::setOnLocalCandidate(CandidateCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onLocalCandidate_ = cb;
}

bool NetworkEngine::sendMidi(const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (midiChannel_ && midiChannel_->isOpen()) {
        return midiChannel_->send(reinterpret_cast<const rtc::byte*>(payload.data()), payload.size());
    }
    return false;
}

bool NetworkEngine::sendControl(const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (controlChannel_ && controlChannel_->isOpen()) {
        return controlChannel_->send(reinterpret_cast<const rtc::byte*>(payload.data()), payload.size());
    }
    return false;
}

bool NetworkEngine::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pc_) return false;
    return pc_->state() == rtc::PeerConnection::State::Connected;
}

void NetworkEngine::setupDataChannelCallbacks(std::shared_ptr<rtc::DataChannel> channel, bool isMidi) {
    if (!channel) return;

    channel->onMessage([this, isMidi](rtc::message_variant data) {
        if (std::holds_alternative<rtc::binary>(data)) {
            const auto& bin = std::get<rtc::binary>(data);
            std::vector<uint8_t> payload(bin.size());
            std::transform(bin.begin(), bin.end(), payload.begin(), [](auto b) {
                return static_cast<uint8_t>(b);
            });

            BinaryCallback cb;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                cb = isMidi ? onMidiReceived_ : onControlReceived_;
            }
            if (cb) {
                cb(payload);
            }
        }
    });
}

} // namespace MidiWire