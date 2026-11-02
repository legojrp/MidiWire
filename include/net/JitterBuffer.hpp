#pragma once

#include "core/MidiPacket.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

namespace MidiWire {

class JitterBuffer {
public:
    explicit JitterBuffer(uint32_t initialDepthMs = 15);

    void push(const MidiPacket& packet);
    bool popReadyPacket(uint64_t currentLocalTimeNs, MidiPacket& outPacket);

    void setBufferDepthMs(uint32_t ms);
    uint32_t getBufferDepthMs() const;

    size_t size() const;
    void clear();

private:
    struct PacketComparator {
        bool operator()(const MidiPacket& a, const MidiPacket& b) const {
            return a.timestampNs > b.timestampNs;
        }
    };

    std::priority_queue<MidiPacket, std::vector<MidiPacket>, PacketComparator> queue_;
    mutable std::mutex mutex_;
    uint32_t bufferDepthMs_{15};
};

} // namespace MidiWire