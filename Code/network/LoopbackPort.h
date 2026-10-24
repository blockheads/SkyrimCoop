#pragma once

#include "ENetInterface.h"

#include <NoCopyMove.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Net
{
// A UDP socket on 127.0.0.1, one end of a tunnel: an enet host in this process sends to it, and what comes through the
// tunnel goes back to that host's port. Non-blocking; polled.
struct LoopbackPort
{
    struct Datagram
    {
        uint16_t From{}; // the sender's port
        std::vector<uint8_t> Data;
    };

    LoopbackPort() noexcept;
    ~LoopbackPort();
    NO_COPY_MOVE(LoopbackPort);

    // Binds a free port.
    bool Open() noexcept;
    void Close() noexcept;
    [[nodiscard]] uint16_t Port() const noexcept { return m_port; }
    [[nodiscard]] bool IsOpen() const noexcept { return m_port != 0; }

    // Every datagram waiting.
    std::vector<Datagram> Receive() noexcept;
    bool SendTo(uint16_t aPort, const void* apData, size_t aSize) noexcept;

private:
    ENetSocket m_socket;
    uint16_t m_port{};
};
} // namespace Net
