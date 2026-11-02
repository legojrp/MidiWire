#include "midi/WinMidiDriver.hpp"
#include "net/ClockSynchronizer.hpp"
#include <iostream>

namespace MidiWire {

WinMidiDriver::WinMidiDriver() = default;

WinMidiDriver::~WinMidiDriver() {
    closeInputPort();
    std::lock_guard<std::mutex> lock(mutex_);
    if (hMidiOut_) {
        midiOutClose(hMidiOut_);
        hMidiOut_ = nullptr;
    }
}

bool WinMidiDriver::initialize() {
    return true; // WinMM system calls do not require global context init
}

std::vector<std::string> WinMidiDriver::getAvailableInputs() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> devices;
    UINT numDevs = midiInGetNumDevs();

    for (UINT i = 0; i < numDevs; ++i) {
        MIDIINCAPSA caps;
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            devices.push_back(caps.szPname);
        }
    }
    return devices;
}

bool WinMidiDriver::openInputPort(uint32_t index, MidiInputCallback callback) {
    closeInputPort();
    std::lock_guard<std::mutex> lock(mutex_);

    inputCallback_ = callback;
    MMRESULT res = midiInOpen(&hMidiIn_, index, reinterpret_cast<DWORD_PTR>(&WinMidiDriver::midiInProc),
                              reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);

    if (res != MMSYSERR_NOERROR) return false;

    midiInStart(hMidiIn_);
    return true;
}

void WinMidiDriver::closeInputPort() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hMidiIn_) {
        midiInStop(hMidiIn_);
        midiInReset(hMidiIn_);
        midiInClose(hMidiIn_);
        hMidiIn_ = nullptr;
    }
}

bool WinMidiDriver::createVirtualOutputPort(const std::string& portName) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Route to default MIDI Mapper output device (Device 0)
    MMRESULT res = midiOutOpen(&hMidiOut_, 0, 0, 0, CALLBACK_NULL);
    return res == MMSYSERR_NOERROR;
}

void WinMidiDriver::sendToVirtualPort(const MidiPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hMidiOut_) return;

    DWORD msg = (packet.status | (packet.channel & 0x0F)) |
                (packet.data1 << 8) |
                (packet.data2 << 16);

    midiOutShortMsg(hMidiOut_, msg);
}

void CALLBACK WinMidiDriver::midiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, 
                                        DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (wMsg == MIM_DATA) {
        auto* driver = reinterpret_cast<WinMidiDriver*>(dwInstance);
        if (!driver) return;

        DWORD msg = static_cast<DWORD>(dwParam1);
        uint8_t status = msg & 0xFF;
        uint8_t data1 = (msg >> 8) & 0xFF;
        uint8_t data2 = (msg >> 16) & 0xFF;

        MidiPacket packet{};
        packet.timestampNs = ClockSynchronizer::getCurrentMonotonicNs();
        packet.status = status & 0xF0;
        packet.channel = status & 0x0F;
        packet.data1 = data1;
        packet.data2 = data2;

        MidiInputCallback cb;
        {
            std::lock_guard<std::mutex> lock(driver->mutex_);
            cb = driver->inputCallback_;
        }
        if (cb) cb(packet);
    }
}

} // namespace MidiWire