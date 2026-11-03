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

        uint8_t status = dwParam1 & 0xFF;
        uint8_t data1  = (dwParam1 >> 8) & 0xFF;
        uint8_t data2  = (dwParam1 >> 16) & 0xFF;

        std::vector<uint8_t> midiData = { status, data1, data2 };

        printMidiBytes("DRIVER IN", midiData.data(), midiData.size());

        if (driver->inputCallback_) {
            driver->inputCallback_(midiData); // Correct 1-argument call
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

    printMidiBytes("DRIVER OUT", packet.data.data(), packet.data.size());

    if (packet.data.size() >= 3) {
        DWORD msg = packet.data[0] | (packet.data[1] << 8) | (packet.data[2] << 16);
        midiOutShortMsg(hMidiOut_, msg);
    } else if (packet.data.size() == 2) {
        DWORD msg = packet.data[0] | (packet.data[1] << 8);
        midiOutShortMsg(hMidiOut_, msg);
    }
}

} // namespace MidiWire