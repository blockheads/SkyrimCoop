#pragma once
#include <cstddef>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include "line_buffer.h"

class TcpClient {
public:
    ~TcpClient();

    // Connect to localhost:port. Returns true on success.
    bool Connect(uint16_t aPort);
    void Disconnect();

    // Send raw bytes. Returns false on error.
    // Used internally by SendJsonLine and by legacy callers (AnimationSystem, etc.)
    // until Plan 04 converts all command senders to JSON.
    bool Send(const void* aData, uint32_t aSize);

    // Send a JSON message as an NDJSON line (serialized + "\n")
    bool SendJsonLine(const nlohmann::json& aMsg);

    // Receive next NDJSON line from the DLL. Uses internal LineBuffer.
    // Returns false on disconnect or socket error.
    bool ReceiveJsonLine(nlohmann::json& aOut);

    // Send a control message: {"op": aOp}
    bool SendControlJson(const char* aOp);

    bool IsConnected() const { return m_socket != -1; }

private:
    int m_socket = -1;
    LineBuffer m_lineBuffer;

    // Helper: read exactly `len` bytes from socket
    bool ReadExact(void* aBuf, size_t aLen);
};
