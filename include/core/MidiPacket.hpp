#pragma once

#include <cstdint>
#include <vector>

namespace MidiWire {

/**
 * @brief Primary normalized data container for MIDI messages across threads and queues.
 */
struct MidiPacket {
    // Attributes with default member initializers
    uint64_t timestampNs = 0;            ///< Monotonic nanosecond timestamp (0 if immediate/non-timestamped)
    uint8_t status = 0;                  ///< Raw MIDI status byte (e.g., 0x90, 0x80, 0xB0)
    uint8_t data1 = 0;                   ///< First data byte (Key/Note number, Controller ID)
    uint8_t data2 = 0;                   ///< Second data byte (Velocity, Controller value)
    uint8_t channel = 0;                 ///< MIDI channel index (0–15)
    std::vector<uint8_t> sysex{};        ///< Optional variable-length System Exclusive payload

    /**
     * @brief Default constructor.
     */
    MidiPacket() = default;

    /**
     * @brief Parameterized constructor for standard MIDI events.
     * 
     * @param status Raw status byte.
     * @param data1 First data byte.
     * @param data2 Second data byte.
     * @param channel MIDI channel (0-15).
     * @param timestampNs Monotonic nanosecond timestamp.
     */
    MidiPacket(uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0, uint8_t channel = 0, uint64_t timestampNs = 0)
        : timestampNs(timestampNs),
          status(status),
          data1(data1),
          data2(data2),
          channel(channel) {}

    /**
     * @brief Checks if the packet represents a System Exclusive message.
     * 
     * @return true if status byte is 0xF0 or the sysex vector contains data.
     */
    [[nodiscard]] bool isSysEx() const {
        return (status == 0xF0) || !sysex.empty();
    }
};

} // namespace MidiWire