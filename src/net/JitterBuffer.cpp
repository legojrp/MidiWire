#include "net/JitterBuffer.hpp"

namespace MidiWire {

JitterBuffer::JitterBuffer(uint32_t initialDepthMs)
    : bufferDepthMs_(initialDepthMs) {}

void JitterBuffer::push(const MidiPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(packet);
}

bool JitterBuffer::popReadyPacket(uint64_t currentLocalTimeNs, MidiPacket& outPacket) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!queue_.empty() && queue_.top().timestampNs <= currentLocalTimeNs) {
        outPacket = queue_.top();
        queue_.pop();
        return true;
    }
    return false;
}

void JitterBuffer::setBufferDepthMs(uint32_t ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    bufferDepthMs_ = ms;
}

uint32_t JitterBuffer::getBufferDepthMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bufferDepthMs_;
}

size_t JitterBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void JitterBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_ = std::priority_queue<MidiPacket, std::vector<MidiPacket>, PacketComparator>();
}

} // namespace MidiWire