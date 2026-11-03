#pragma once

#include "core/MidiPacket.hpp"
#include <vector>
#include <cstdint>
#include <cstring>

// DEBUG

#include <iostream>
#include <iomanip>
#include <vector>

inline void printMidiBytes(const std::string& label, const uint8_t* data, size_t length) {
    std::cout << "[" << label << "] (" << length << " bytes): ";
    for (size_t i = 0; i < length; ++i) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(data[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

// END DEBUG


namespace MidiWire {

class Serializer {
public:
    Serializer() = delete;

    /**
     * @brief Serializes a MidiPacket into a 12-byte binary wire format (plus optional SysEx payload).
     *
     * Wire Protocol Structure:
     * - Bytes 0-7  : timestampNs (uint64_t, Big-Endian)
     * - Byte 8     : status (uint8_t)
     * - Byte 9     : data1 (uint8_t)
     * - Byte 10    : data2 (uint8_t)
     * - Byte 11    : channel (uint8_t)
     * - Bytes 12-15: [Optional SysEx] payload byte length (uint32_t, Big-Endian)
     * - Bytes 16+  : [Optional SysEx] payload bytes
     *
     * @param packet The MidiPacket structure to pack.
     * @return std::vector<uint8_t> Packed binary byte vector.
     */
    static std::vector<uint8_t> serialize(const MidiPacket& packet);

    /**
     * @brief Parses a raw binary memory buffer into a MidiPacket struct.
     *
     * @param buffer Pointer to the raw byte buffer.
     * @param length Total length of the buffer in bytes.
     * @return MidiPacket Unpacked packet, or empty/default MidiPacket if length < 12 or buffer is null.
     */
    static MidiPacket deserialize(const uint8_t* buffer, size_t length);

    /**
     * @brief Overload that deserializes a std::vector<uint8_t> by forwarding to the pointer implementation.
     *
     * @param buffer Binary buffer containing serialized MIDI data.
     * @return MidiPacket Unpacked packet struct.
     */
    static MidiPacket deserialize(const std::vector<uint8_t>& buffer);
};

} // namespace MidiWire