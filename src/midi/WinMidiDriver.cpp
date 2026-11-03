#include "midi/WinMidiDriver.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

namespace MidiWire {

// Helper function to format and print raw MIDI bytes
static void printMidiBytes(const std::string& label, const uint8_t* data, size_t length) {
    std::cout << "[" << label << "] (" << length << " bytes): ";
    for (size_t i = 0; i < length; ++i) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(data[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

void CALLBACK WinMidiDriver::midiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (wMsg == MIM_DATA) {
        auto* driver = reinterpret_cast<WinMidiDriver*>(dwInstance);
        if (!driver) return;

        uint8_t rawStatus = dwParam1 & 0xFF;
        uint8_t data1     = (dwParam1 >> 8) & 0xFF;
        uint8_t data2     = (dwParam1 >> 16) & 0xFF;
        uint8_t channel   = rawStatus & 0x0F;

        MidiPacket packet(rawStatus, data1, data2, channel, static_cast<uint64_t>(dwParam2));

        std::vector<uint8_t> rawBytes = { packet.status, packet.data1, packet.data2 };
        printMidiBytes("DRIVER IN", rawBytes.data(), rawBytes.size());

        if (driver->inputCallback_) {
            driver->inputCallback_(packet);
        }
    }
}

WinMidiDriver::WinMidiDriver() : hMidiIn_(nullptr), hMidiOut_(nullptr) {}

WinMidiDriver::~WinMidiDriver() {
    closeInputPort();
    if (hMidiOut_) {
        midiOutClose(hMidiOut_);
        hMidiOut_ = nullptr;
    }
}

bool WinMidiDriver::initialize() {
    return true;
}

std::vector<std::string> WinMidiDriver::getAvailableInputs() {
    std::vector<std::string> inputs;
    UINT numDevs = midiInGetNumDevs();
    for (UINT i = 0; i < numDevs; ++i) {
        MIDIINCAPS caps;
        if (midiInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            #ifdef UNICODE
            std::wstring wName(caps.szPname);
            std::string name(wName.begin(), wName.end());
            #else
            std::string name(caps.szPname);
            #endif
            inputs.push_back(name);
        }
    }
    return inputs;
}

bool WinMidiDriver::openInputPort(uint32_t index, MidiInputCallback callback) {
    closeInputPort();
    inputCallback_ = callback;

    MMRESULT res = midiInOpen(&hMidiIn_, index, reinterpret_cast<DWORD_PTR>(&WinMidiDriver::midiInProc), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        std::cerr << "[WinMidiDriver] Failed to open MIDI input port " << index << std::endl;
        return false;
    }

    midiInStart(hMidiIn_);
    return true;
}

void WinMidiDriver::closeInputPort() {
    if (hMidiIn_) {
        midiInStop(hMidiIn_);
        midiInClose(hMidiIn_);
        hMidiIn_ = nullptr;
    }
}

bool WinMidiDriver::createVirtualOutputPort(const std::string& portName) {
    UINT numDevs = midiOutGetNumDevs();
    UINT targetDev = 0;
    for (UINT i = 0; i < numDevs; ++i) {
        MIDIOUTCAPS caps;
        if (midiOutGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            #ifdef UNICODE
            std::wstring wName(caps.szPname);
            std::string name(wName.begin(), wName.end());
            #else
            std::string name(caps.szPname);
            #endif
            if (name.find(portName) != std::string::npos || name.find("loopMIDI") != std::string::npos) {
                targetDev = i;
                break;
            }
        }
    }

    MMRESULT res = midiOutOpen(&hMidiOut_, targetDev, 0, 0, CALLBACK_NULL);
    return (res == MMSYSERR_NOERROR);
}

void WinMidiDriver::sendToVirtualPort(const MidiPacket& packet) {
    if (!hMidiOut_) return;

    if (packet.isSysEx()) {
        if (!packet.sysex.empty()) {
            printMidiBytes("DRIVER OUT SYSEX", packet.sysex.data(), packet.sysex.size());
        }
        return;
    }

    std::vector<uint8_t> rawBytes = { packet.status, packet.data1, packet.data2 };
    printMidiBytes("DRIVER OUT", rawBytes.data(), rawBytes.size());

    DWORD msg = packet.status | (packet.data1 << 8) | (packet.data2 << 16);
    midiOutShortMsg(hMidiOut_, msg);
}

} // namespace MidiWire