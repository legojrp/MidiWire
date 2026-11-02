#include "midi/MacMidiDriver.hpp"

namespace MidiWire {

MacMidiDriver::MacMidiDriver() = default;

MacMidiDriver::~MacMidiDriver() {
    closeInputPort();
    if (virtualSource_ != 0) {
        MIDIEndpointDispose(virtualSource_);
        virtualSource_ = 0;
    }
    if (client_ != 0) {
        MIDIClientDispose(client_);
        client_ = 0;
    }
}

bool MacMidiDriver::initialize() {
    if (client_ != 0) {
        return true;
    }

    CFStringRef clientName = CFStringCreateWithCString(kCFAllocatorDefault, "MidiWireMacClient", kCFStringEncodingUTF8);
    if (!clientName) {
        return false;
    }

    OSStatus status = MIDIClientCreate(clientName, nullptr, nullptr, &client_);
    CFRelease(clientName);

    return status == noErr;
}

std::vector<std::string> MacMidiDriver::getAvailableInputs() {
    std::vector<std::string> inputs;
    ItemCount count = MIDIGetNumberOfSources();

    for (ItemCount i = 0; i < count; ++i) {
        MIDIEndpointRef source = MIDIGetSource(i);
        if (source == 0) {
            continue;
        }

        CFStringRef nameRef = nullptr;
        OSStatus status = MIDIObjectGetStringProperty(source, kMIDIPropertyName, &nameRef);
        if (status == noErr && nameRef != nullptr) {
            char buffer[256];
            if (CFStringGetCString(nameRef, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                inputs.push_back(buffer);
            } else {
                inputs.push_back("Unknown");
            }
            CFRelease(nameRef);
        } else {
            inputs.push_back("Unknown");
        }
    }

    return inputs;
}

bool MacMidiDriver::openInputPort(uint32_t index, MidiInputCallback callback) {
    if (client_ == 0 && !initialize()) {
        return false;
    }

    if (index >= MIDIGetNumberOfSources()) {
        return false;
    }

    closeInputPort();
    inputCallback_ = std::move(callback);

    CFStringRef portName = CFStringCreateWithCString(kCFAllocatorDefault, "MidiWireInputPort", kCFStringEncodingUTF8);
    if (!portName) {
        inputCallback_ = nullptr;
        return false;
    }

    OSStatus status = MIDIInputPortCreate(client_, portName, coreMidiReadProc, this, &inputPort_);
    CFRelease(portName);

    if (status != noErr) {
        inputCallback_ = nullptr;
        return false;
    }

    MIDIEndpointRef source = MIDIGetSource(index);
    if (source == 0) {
        closeInputPort();
        return false;
    }

    status = MIDIPortConnectSource(inputPort_, source, nullptr);
    if (status != noErr) {
        closeInputPort();
        return false;
    }

    return true;
}

void MacMidiDriver::closeInputPort() {
    if (inputPort_ != 0) {
        MIDIPortDispose(inputPort_);
        inputPort_ = 0;
    }
    inputCallback_ = nullptr;
}

bool MacMidiDriver::createVirtualOutputPort(const std::string& portName) {
    if (client_ == 0 && !initialize()) {
        return false;
    }

    if (virtualSource_ != 0) {
        MIDIEndpointDispose(virtualSource_);
        virtualSource_ = 0;
    }

    CFStringRef cfName = CFStringCreateWithCString(kCFAllocatorDefault, portName.c_str(), kCFStringEncodingUTF8);
    if (!cfName) {
        return false;
    }

    OSStatus status = MIDISourceCreate(client_, cfName, &virtualSource_);
    CFRelease(cfName);

    return status == noErr;
}

void MacMidiDriver::sendToVirtualPort(const MidiPacket& packet) {
    if (virtualSource_ == 0) {
        return;
    }

    Byte buffer[512];
    MIDIPacketList* pktList = reinterpret_cast<MIDIPacketList*>(buffer);
    MIDIPacket* curPkt = MIDIPacketListInit(pktList);

    Byte payload[3] = { packet.status, packet.data1, packet.data2 };
    curPkt = MIDIPacketListAdd(pktList, sizeof(buffer), curPkt, 0, 3, payload);

    if (curPkt != nullptr) {
        MIDIReceived(virtualSource_, pktList);
    }
}

void MacMidiDriver::coreMidiReadProc(const MIDIPacketList *pktlist, void *readProcRefCon, void *srcConnRefCon) {
    (void)srcConnRefCon;

    if (!pktlist || !readProcRefCon) {
        return;
    }

    auto* driver = static_cast<MacMidiDriver*>(readProcRefCon);
    if (!driver->inputCallback_) {
        return;
    }

    const MIDIPacket* pkt = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        if (pkt->length > 0) {
            MidiPacket midiPacket{};
            midiPacket.status = pkt->data[0];
            midiPacket.data1 = (pkt->length > 1) ? pkt->data[1] : 0;
            midiPacket.data2 = (pkt->length > 2) ? pkt->data[2] : 0;
            midiPacket.timestampNs = pkt->timeStamp;

            driver->inputCallback_(midiPacket);
        }
        pkt = MIDIPacketNext(pkt);
    }
}

} // namespace MidiWire