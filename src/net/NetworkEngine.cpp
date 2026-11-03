#include "net/NetworkEngine.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace MidiWire {

static void printHex(const std::string& label, const std::vector<uint8_t>& bytes) {
    std::cout << "[" << label << "] (" << bytes.size() << " bytes): ";
    for (uint8_t b : bytes) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(b) << " ";
    }
    std::cout << std::dec << std::endl;
}

NetworkEngine::NetworkEngine() = default;

NetworkEngine::~NetworkEngine() {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> midiChan;
    std::shared_ptr<rtc::DataChannel> ctrlChan;

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
        pendingCandidates_.clear();
        hasRemoteDescription_ = false;
    }

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

    hasRemoteDescription_ = false;
    pendingCandidates_.clear();

    rtc::Configuration config;
    if (!stunServer.empty()) {
        config.iceServers.emplace_back(stunServer);
    }
    config.iceServers.emplace_back("stun:stun1.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun2.l.google.com:19302");

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
        std::cout << "[NET PEER STATE] PeerConnection state: " << static_cast<int>(state) << std::endl;
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
        std::cout << "[NET DATA CHANNEL RECV] Remote created channel: " << channel->label() << std::endl;
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
    hasRemoteDescription_ = true;

    // Flush queued candidates received prior to SDP exchange
    for (const auto& cand : pendingCandidates_) {
        pc_->addRemoteCandidate(cand);
    }
    pendingCandidates_.clear();

    if (type == "offer") {
        pc_->setLocalDescription();
    }
}

void NetworkEngine::handleRemoteCandidate(const std::string& candidate, const std::string& mid) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pc_) return;

    rtc::Candidate cand(candidate, mid);
    if (hasRemoteDescription_) {
        pc_->addRemoteCandidate(cand);
    } else {
        pendingCandidates_.push_back(cand);
    }
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
        printHex("NET SEND MIDI", payload);
        return midiChannel_->send(reinterpret_cast<const rtc::byte*>(payload.data()), payload.size());
    } else {
        std::cerr << "[NET SEND FAILED] Cannot send MIDI — midiChannel_ is NOT open!" << std::endl;
    }
    return false;
}

bool NetworkEngine::sendControl(const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (controlChannel_ && controlChannel_->isOpen()) {
        printHex("NET SEND CTRL", payload);
        return controlChannel_->send(reinterpret_cast<const rtc::byte*>(payload.data()), payload.size());
    } else {
        std::cerr << "[NET SEND FAILED] Cannot send Control — controlChannel_ is NOT open!" << std::endl;
    }
    return false;
}

bool NetworkEngine::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (midiChannel_ && midiChannel_->isOpen()) {
        return true;
    }
    if (!pc_) return false;
    return pc_->state() == rtc::PeerConnection::State::Connected;
}

void NetworkEngine::setupDataChannelCallbacks(std::shared_ptr<rtc::DataChannel> channel, bool isMidi) {
    if (!channel) return;

    if (channel->isOpen()) {
        std::cout << "[NET CHANNEL OPENED] DataChannel '" << channel->label() << "' is ready for transmission!" << std::endl;
    }

    channel->onOpen([label = channel->label()]() {
        std::cout << "[NET CHANNEL OPENED] DataChannel '" << label << "' is ready for transmission!" << std::endl;
    });

    channel->onMessage([this, isMidi](rtc::message_variant data) {
        if (std::holds_alternative<rtc::binary>(data)) {
            const auto& bin = std::get<rtc::binary>(data);
            std::vector<uint8_t> payload(bin.size());
            std::transform(bin.begin(), bin.end(), payload.begin(), [](auto b) {
                return static_cast<uint8_t>(b);
            });

            printHex(isMidi ? "NET RECV MIDI" : "NET RECV CTRL", payload);

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