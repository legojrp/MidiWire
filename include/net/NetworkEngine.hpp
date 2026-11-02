#pragma once

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <cstdint>
#include <variant>

namespace MidiWire {

class NetworkEngine {
public:
    using BinaryCallback = std::function<void(const std::vector<uint8_t>&)>;
    using StateCallback = std::function<void(rtc::PeerConnection::State)>;
    using DescriptionCallback = std::function<void(const std::string& type, const std::string& sdp)>;
    using CandidateCallback = std::function<void(const std::string& candidate, const std::string& mid)>;

    NetworkEngine();
    ~NetworkEngine();

    void initialize(const std::string& stunServer = "stun:stun.l.google.com:19302");
    void createOffer();
    void handleRemoteDescription(const std::string& type, const std::string& sdp);
    void handleRemoteCandidate(const std::string& candidate, const std::string& mid);

    void setOnMidiReceived(BinaryCallback cb);
    void setOnControlReceived(BinaryCallback cb);
    void setOnStateChanged(StateCallback cb);
    void setOnLocalDescription(DescriptionCallback cb);
    void setOnLocalCandidate(CandidateCallback cb);

    bool sendMidi(const std::vector<uint8_t>& payload);
    bool sendControl(const std::vector<uint8_t>& payload);
    bool isConnected() const;

private:
    void setupDataChannelCallbacks(std::shared_ptr<rtc::DataChannel> channel, bool isMidi);

    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> midiChannel_;
    std::shared_ptr<rtc::DataChannel> controlChannel_;

    BinaryCallback onMidiReceived_;
    BinaryCallback onControlReceived_;
    StateCallback onStateChanged_;
    DescriptionCallback onLocalDescription_;
    CandidateCallback onLocalCandidate_;

    mutable std::mutex mutex_;
};

} // namespace MidiWire