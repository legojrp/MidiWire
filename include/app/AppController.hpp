#pragma once

#include "midi/MidiEngine.hpp"
#include "net/NetworkEngine.hpp"
#include "net/SignalingClient.hpp"
#include "net/JitterBuffer.hpp"
#include "net/ClockSynchronizer.hpp"
#include "core/Serializer.hpp"

#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <iostream>

namespace MidiWire {

class IMidiDriver;

class AppController {
public:
    explicit AppController(std::unique_ptr<IMidiDriver> midiDriver);
    ~AppController();

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;
    AppController(AppController&&) = delete;
    AppController& operator=(AppController&&) = delete;

    bool initialize();
    void connectToSignaling(const std::string& signalingUrl);
    void joinRoom(const std::string& roomId);
    std::vector<std::string> getAvailableMidiDevices();
    bool selectMidiInputDevice(uint32_t index);
    bool setupVirtualMidiPort(const std::string& portName = "MidiWire Remote In");
    void setJitterBufferDepthMs(uint32_t ms);
    void panic();
    void start();
    void stop();

private:
    void playbackLoop();
    void handleLocalMidiInput(const MidiPacket& packet);
    void onLocalMidiPacket(const MidiPacket& packet);

    std::unique_ptr<IMidiDriver> midiDriver_;
    std::unique_ptr<MidiEngine> midiEngine_;
    std::unique_ptr<NetworkEngine> netEngine_;
    std::unique_ptr<SignalingClient> signalingClient_;
    JitterBuffer jitterBuffer_{15};
    ClockSynchronizer clockSync_;
    std::thread playbackThread_;
    std::atomic<bool> running_{false};
    std::string currentRoomId_;
};

} // namespace MidiWire