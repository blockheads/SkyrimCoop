#pragma once
#include <cstddef>
#include <cstdint>
#include "protocol.h"

class TcpClient {
public:
    ~TcpClient();

    // Connect to localhost:port. Returns true on success.
    bool Connect(uint16_t aPort);
    void Disconnect();

    // Send raw bytes. Returns false on error.
    bool Send(const void* aData, uint32_t aSize);

    // Receive next packet. Blocks until header + payload available.
    // Returns false on disconnect or error.
    bool Receive(PacketHeader* apOutHeader, void* apOutPayload, uint32_t aMaxPayload);

    // Send a control packet (e.g., CTRL_READY)
    bool SendControl(uint16_t aOpcode);

    bool IsConnected() const { return m_socket != -1; }

private:
    int m_socket = -1;

    // Helper: read exactly `len` bytes from socket
    bool ReadExact(void* aBuf, size_t aLen);
};
