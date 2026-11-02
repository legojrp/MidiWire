#include "midi/MidiEngine.hpp"

namespace MidiWire {

MidiEngine::MidiEngine(std::unique_ptr<IMidiDriver> driver)
    : driver_(std::move(driver)) {
}

bool MidiEngine::initialize() {
    if (!driver_) {
        initialized_ = false;
        return false;
    }
    initialized_ = driver_->initialize();
    return initialized_;
}

std::vector<std::string> MidiEngine::refreshInputDevices() {
    if (!driver_) {
        return {};
    }
    return driver_->getAvailableInputs();
}

bool MidiEngine::startInputDevice(uint32_t deviceIndex, MidiInputCallback callback) {
    if (!driver_) {
        return false;
    }
    bool success = driver_->openInputPort(deviceIndex, std::move(callback));
    if (success) {
        activeInputIndex_ = static_cast<int>(deviceIndex);
    }
    return success;
}

void MidiEngine::stopInputDevice() {
    if (driver_) {
        driver_->closeInputPort();
    }
    activeInputIndex_ = -1;
}

bool MidiEngine::setupVirtualPort(const std::string& portName) {
    if (!driver_) {
        return false;
    }
    bool success = driver_->createVirtualOutputPort(portName);
    if (success) {
        activeVirtualPortName_ = portName;
    }
    return success;
}

void MidiEngine::sendToVirtualPort(const MidiPacket& packet) {
    if (driver_) {
        driver_->sendToVirtualPort(packet);
    }
}

void MidiEngine::panic() {
    if (!driver_) {
        return;
    }

    for (uint8_t ch = 0; ch < 16; ++ch) {
        // CC 123: All Notes Off
        MidiPacket notesOff{};
        notesOff.timestampNs = 0;
        notesOff.status = static_cast<uint8_t>(0xB0 | ch);
        notesOff.data1 = 123;
        notesOff.data2 = 0;
        notesOff.channel = ch;
        driver_->sendToVirtualPort(notesOff);

        // CC 120: All Sound Off
        MidiPacket soundOff{};
        soundOff.timestampNs = 0;
        soundOff.status = static_cast<uint8_t>(0xB0 | ch);
        soundOff.data1 = 120;
        soundOff.data2 = 0;
        soundOff.channel = ch;
        driver_->sendToVirtualPort(soundOff);
    }
}

} // namespace MidiWire