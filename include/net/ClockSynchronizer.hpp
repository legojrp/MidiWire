#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

namespace MidiWire {

class ClockSynchronizer {
public:
    ClockSynchronizer() = default;

    /**
     * @brief Returns current monotonic time reading from std::chrono::steady_clock in nanoseconds.
     */
    static uint64_t getCurrentMonotonicNs();

    /**
     * @brief Processes NTP-style 4-timestamp exchange to calculate RTT and clock offset.
     * @param t1_local Local transmit timestamp (nanoseconds)
     * @param t2_remote Remote receive timestamp (nanoseconds)
     * @param t3_remote Remote transmit timestamp (nanoseconds)
     * @param t4_local Local receive timestamp (nanoseconds)
     */
    void processPong(uint64_t t1_local, uint64_t t2_remote, uint64_t t3_remote, uint64_t t4_local);

    /**
     * @brief Translates an incoming remote steady_clock timestamp to the local steady_clock timeline.
     */
    uint64_t remoteToLocalTime(uint64_t remoteTimestampNs) const;

    /**
     * @brief Returns the currently estimated clock offset in nanoseconds (remote_time - local_time).
     */
    int64_t getClockOffsetNs() const;

    /**
     * @brief Returns the currently estimated round-trip time in nanoseconds.
     */
    uint64_t getRoundTripTimeNs() const;

    /**
     * @brief Resets clock offset and round-trip time estimates back to zero.
     */
    void reset();

private:
    int64_t clockOffsetNs_{0};     // Estimated remote_time - local_time in nanoseconds
    uint64_t roundTripTimeNs_{0};  // Current RTT estimation in nanoseconds
    mutable std::mutex mutex_;
};

} // namespace MidiWire