#pragma once
#include <winsock2.h>
#include <cstdint>
#include "protocol.h"

class TcpServer {
public:
    // Bind to localhost:0, listen, store assigned port. Returns true on success.
    bool Start();
    void Stop();

    // Accept a single client (blocking). Returns true when client connects.
    bool AcceptClient();

    // Close current client connection (if any).
    void DisconnectClient();

    // Send a packet to the connected client. Thread-safe (called from hook trampolines).
    bool Send(const void* data, uint32_t size);

    // Receive a packet from the client (blocking read of header, then payload).
    // Returns false on disconnect.
    bool Receive(PacketHeader* outHeader, void* outPayload, uint32_t maxPayload);

    uint16_t GetPort() const { return m_port; }
    bool IsClientConnected() const { return m_clientSocket != INVALID_SOCKET; }

private:
    // Receive exactly 'size' bytes into 'buf'. Returns false on error/disconnect.
    bool RecvExact(void* buf, uint32_t size);

    SOCKET m_listenSocket = INVALID_SOCKET;
    SOCKET m_clientSocket = INVALID_SOCKET;
    uint16_t m_port = 0;
    CRITICAL_SECTION m_sendLock;  // protects Send() from concurrent hook trampolines
    bool m_initialized = false;
};
