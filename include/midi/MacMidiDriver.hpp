#pragma once

#if defined(__APPLE__)

#include "midi/IMidiDriver.hpp"
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <atomic>
#include <mutex>

namespace MidiWire {

class MacMidiDriver : public IMidiDriver {
public:
    MacMidiDriver();
    ~MacMidiDriver() override;

    bool initialize() override;
    std::vector<std::string> getAvailableInputs() override;
    bool openInputPort(uint32_t index, MidiInputCallback callback) override;
    void closeInputPort() override;
    bool createVirtualOutputPort(const std::string& portName) override;
    void sendToVirtualPort(const MidiPacket& packet) override;

private:
    MIDIClientRef client_{0};
    MIDIPortRef inputPort_{0};
    MIDIEndpointRef virtualSource_{0};
    MidiInputCallback inputCallback_;
    mutable std::mutex mutex_;

    static void coreMidiReadProc(const MIDIPacketList* pktlist, void* readProcRefCon, void* srcConnRefCon);
};

} // namespace MidiWire

#endif // __APPLE__