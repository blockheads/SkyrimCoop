#pragma once
#include <atomic>
#include <cstdint>
#include "protocol.h"

struct CommandSlot {
    uint16_t opcode;
    uint8_t argCount;
    uint64_t args[8];
};

class CommandQueue {
public:
    static constexpr uint32_t QUEUE_SIZE = 256;  // must be power of 2

    // Returns true if enqueued, false if full
    bool Enqueue(const CommandSlot& cmd);

    // Drains all available commands, calls fn(const CommandSlot&) for each
    // Returns number of commands drained
    template<typename Fn>
    uint32_t Drain(Fn&& fn) {
        uint32_t r = m_readPos.load(std::memory_order_relaxed);
        uint32_t w = m_writePos.load(std::memory_order_acquire);
        uint32_t count = 0;
        while (r != w) {
            fn(m_slots[r & (QUEUE_SIZE - 1)]);
            r++;
            count++;
        }
        m_readPos.store(r, std::memory_order_release);
        return count;
    }

    uint32_t Size() const;
    bool IsFull() const;

private:
    CommandSlot m_slots[QUEUE_SIZE]{};
    alignas(64) std::atomic<uint32_t> m_writePos{0};
    alignas(64) std::atomic<uint32_t> m_readPos{0};
};
