#pragma once

#include "midi/IMidiDriver.hpp"
#include <alsa/asoundlib.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

namespace MidiWire {

class AlsaMidiDriver : public IMidiDriver {
public:
    AlsaMidiDriver();
    ~AlsaMidiDriver() override;

    bool initialize() override;
    std::vector<std::string> getAvailableInputs() override;
    bool openInputPort(uint32_t index, MidiInputCallback callback) override;
    void closeInputPort() override;
    bool createVirtualOutputPort(const std::string& portName) override;
    void sendToVirtualPort(const MidiPacket& packet) override;

private:
    void readLoop();

    snd_seq_t* seqHandle_{nullptr};
    int inPortId_{-1};
    int virtualOutPortId_{-1};
    
    struct InputPortInfo {
        int client;
        int port;
        std::string name;
    };
    std::vector<InputPortInfo> discoveredPorts_;

    MidiInputCallback inputCallback_;
    std::thread readThread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
};

} // namespace MidiWire