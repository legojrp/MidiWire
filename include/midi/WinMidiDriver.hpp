#pragma once

#include "midi/IMidiDriver.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <mutex>
#include <vector>
#include <string>

namespace MidiWire {

class WinMidiDriver : public IMidiDriver {
public:
    WinMidiDriver();
    ~WinMidiDriver() override;

    bool initialize() override;
    std::vector<std::string> getAvailableInputs() override;
    bool openInputPort(uint32_t index, MidiInputCallback callback) override;
    void closeInputPort() override;
    bool createVirtualOutputPort(const std::string& portName) override;
    void sendToVirtualPort(const MidiPacket& packet) override;

    static void CALLBACK midiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, 
                                     DWORD_PTR dwParam1, DWORD_PTR dwParam2);

private:
    HMIDIIN hMidiIn_{nullptr};
    HMIDIOUT hMidiOut_{nullptr};
    MidiInputCallback inputCallback_;
    mutable std::mutex mutex_;
};

} // namespace MidiWire