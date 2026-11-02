#pragma once

#include "midi/IMidiDriver.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace MidiWire {

class MidiEngine {
public:
    explicit MidiEngine(std::unique_ptr<IMidiDriver> driver);
    ~MidiEngine() = default;

    MidiEngine(const MidiEngine&) = delete;
    MidiEngine& operator=(const MidiEngine&) = delete;
    MidiEngine(MidiEngine&&) noexcept = default;
    MidiEngine& operator=(MidiEngine&&) noexcept = default;

    bool initialize();
    std::vector<std::string> refreshInputDevices();
    bool startInputDevice(uint32_t deviceIndex, MidiInputCallback callback);
    void stopInputDevice();
    bool setupVirtualPort(const std::string& portName = "MidiWire Remote In");
    void sendToVirtualPort(const MidiPacket& packet);
    void panic();

    int getActiveInputIndex() const { return activeInputIndex_; }
    const std::string& getActiveVirtualPortName() const { return activeVirtualPortName_; }
    bool isInitialized() const { return initialized_; }

private:
    std::unique_ptr<IMidiDriver> driver_;
    std::string activeVirtualPortName_;
    int activeInputIndex_{-1};
    bool initialized_{false};
};

} // namespace MidiWire