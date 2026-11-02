#include "net/ClockSynchronizer.hpp"

namespace MidiWire {

uint64_t ClockSynchronizer::getCurrentMonotonicNs() {
    const auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()
    );
}

void ClockSynchronizer::processPong(uint64_t t1_local, uint64_t t2_remote, uint64_t t3_remote, uint64_t t4_local) {
    // RTT = (t4_local - t1_local) - (t3_remote - t2_remote)
    const int64_t localDuration = static_cast<int64_t>(t4_local) - static_cast<int64_t>(t1_local);
    const int64_t remoteDuration = static_cast<int64_t>(t3_remote) - static_cast<int64_t>(t2_remote);
    const int64_t calculatedRtt = localDuration - remoteDuration;

    // Offset = ((t2_remote - t1_local) + (t3_remote - t4_local)) / 2
    const int64_t delta1 = static_cast<int64_t>(t2_remote) - static_cast<int64_t>(t1_local);
    const int64_t delta2 = static_cast<int64_t>(t3_remote) - static_cast<int64_t>(t4_local);
    const int64_t calculatedOffset = (delta1 + delta2) / 2;

    std::lock_guard<std::mutex> lock(mutex_);
    roundTripTimeNs_ = calculatedRtt > 0 ? static_cast<uint64_t>(calculatedRtt) : 0;
    clockOffsetNs_ = calculatedOffset;
}

uint64_t ClockSynchronizer::remoteToLocalTime(uint64_t remoteTimestampNs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint64_t>(static_cast<int64_t>(remoteTimestampNs) - clockOffsetNs_);
}

int64_t ClockSynchronizer::getClockOffsetNs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clockOffsetNs_;
}

uint64_t ClockSynchronizer::getRoundTripTimeNs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return roundTripTimeNs_;
}

void ClockSynchronizer::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    clockOffsetNs_ = 0;
    roundTripTimeNs_ = 0;
}

} // namespace MidiWire