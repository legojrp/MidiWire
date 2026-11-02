#pragma once

#include "core/MidiPacket.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MidiWire {

using MidiInputCallback = std::function<void(const MidiPacket&)>;

class IMidiDriver {
public:
    virtual ~IMidiDriver() = default;

    virtual bool initialize() = 0;
    virtual std::vector<std::string> getAvailableInputs() = 0;
    virtual bool openInputPort(uint32_t index, MidiInputCallback callback) = 0;
    virtual void closeInputPort() = 0;

    virtual bool createVirtualOutputPort(const std::string& portName) = 0;
    virtual void sendToVirtualPort(const MidiPacket& packet) = 0;
};

} // namespace MidiWire