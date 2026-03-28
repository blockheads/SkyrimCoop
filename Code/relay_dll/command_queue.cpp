#include "command_queue.h"

bool CommandQueue::Enqueue(const CommandSlot& cmd) {
    uint32_t w = m_writePos.load(std::memory_order_relaxed);
    uint32_t r = m_readPos.load(std::memory_order_acquire);
    if (w - r >= QUEUE_SIZE)
        return false;  // full
    m_slots[w & (QUEUE_SIZE - 1)] = cmd;
    m_writePos.store(w + 1, std::memory_order_release);
    return true;
}

uint32_t CommandQueue::Size() const {
    uint32_t w = m_writePos.load(std::memory_order_acquire);
    uint32_t r = m_readPos.load(std::memory_order_acquire);
    return w - r;
}

bool CommandQueue::IsFull() const {
    return Size() >= QUEUE_SIZE;
}
