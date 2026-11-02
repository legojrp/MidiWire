#include "core/Serializer.hpp"
#include <cstring>

namespace MidiWire {

std::vector<uint8_t> Serializer::serialize(const MidiPacket& packet) {
    const bool isSysEx = packet.isSysEx();
    const size_t sysExSize = isSysEx ? packet.sysex.size() : 0;
    const size_t totalSize = 12 + (isSysEx ? (4 + sysExSize) : 0);

    std::vector<uint8_t> buffer(totalSize);

    // Pack 64-bit nanosecond timestamp in Big-Endian network byte order
    for (int i = 0; i < 8; ++i) {
        buffer[i] = static_cast<uint8_t>((packet.timestampNs >> (56 - i * 8)) & 0xFF);
    }

    buffer[8]  = packet.status;
    buffer[9]  = packet.data1;
    buffer[10] = packet.data2;
    buffer[11] = packet.channel;

    if (isSysEx) {
        // Pack 32-bit SysEx length prefix in Big-Endian order
        const uint32_t len = static_cast<uint32_t>(sysExSize);
        buffer[12] = static_cast<uint8_t>((len >> 24) & 0xFF);
        buffer[13] = static_cast<uint8_t>((len >> 16) & 0xFF);
        buffer[14] = static_cast<uint8_t>((len >> 8) & 0xFF);
        buffer[15] = static_cast<uint8_t>(len & 0xFF);

        std::memcpy(buffer.data() + 16, packet.sysex.data(), sysExSize);
    }

    return buffer;
}

MidiPacket Serializer::deserialize(const uint8_t* buffer, size_t length) {
    MidiPacket packet{};
    if (length < 12 || buffer == nullptr) {
        return packet;
    }

    // Unpack Big-Endian uint64 timestamp
    packet.timestampNs = 0;
    for (int i = 0; i < 8; ++i) {
        packet.timestampNs = (packet.timestampNs << 8) | buffer[i];
    }

    packet.status  = buffer[8];
    packet.data1   = buffer[9];
    packet.data2   = buffer[10];
    packet.channel = buffer[11];

    if (length >= 16 && (packet.status == 0xF0 || packet.status == 0xF7)) {
        uint32_t sysExLen = 0;
        for (int i = 12; i < 16; ++i) {
            sysExLen = (sysExLen << 8) | buffer[i];
        }

        if (length >= 16 + sysExLen) {
            packet.sysex.assign(buffer + 16, buffer + 16 + sysExLen);
        }
    }

    return packet;
}

MidiPacket Serializer::deserialize(const std::vector<uint8_t>& buffer) {
    return deserialize(buffer.data(), buffer.size());
}

} // namespace MidiWire