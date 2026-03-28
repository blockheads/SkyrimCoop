#include "tcp_server.h"
#include <ws2tcpip.h>
#include <cstring>

bool TcpServer::Start()
{
    // Initialize Winsock (reference-counted, safe to call multiple times)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;

    InitializeCriticalSection(&m_sendLock);
    m_initialized = true;

    // Create TCP socket
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET)
    {
        WSACleanup();
        return false;
    }

    // Bind to localhost:0 (ephemeral port per Pitfall 4)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    addr.sin_port = htons(0);                        // OS assigns port

    if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    // Retrieve assigned port via getsockname
    sockaddr_in boundAddr{};
    int addrLen = sizeof(boundAddr);
    if (getsockname(m_listenSocket, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen) == SOCKET_ERROR)
    {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }
    m_port = ntohs(boundAddr.sin_port);

    // Listen for a single client
    if (listen(m_listenSocket, 1) == SOCKET_ERROR)
    {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    return true;
}

void TcpServer::Stop()
{
    DisconnectClient();

    if (m_listenSocket != INVALID_SOCKET)
    {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    if (m_initialized)
    {
        DeleteCriticalSection(&m_sendLock);
        m_initialized = false;
    }

    WSACleanup();
    m_port = 0;
}

bool TcpServer::AcceptClient()
{
    if (m_listenSocket == INVALID_SOCKET)
        return false;

    // Blocking accept -- waits for a single client connection
    m_clientSocket = accept(m_listenSocket, nullptr, nullptr);
    if (m_clientSocket == INVALID_SOCKET)
        return false;

    // Disable Nagle for low-latency hook event forwarding
    int flag = 1;
    setsockopt(m_clientSocket, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));

    return true;
}

void TcpServer::DisconnectClient()
{
    if (m_clientSocket != INVALID_SOCKET)
    {
        closesocket(m_clientSocket);
        m_clientSocket = INVALID_SOCKET;
    }
}

bool TcpServer::Send(const void* data, uint32_t size)
{
    if (m_clientSocket == INVALID_SOCKET)
        return false;

    EnterCriticalSection(&m_sendLock);

    const char* ptr = static_cast<const char*>(data);
    uint32_t remaining = size;

    while (remaining > 0)
    {
        int sent = send(m_clientSocket, ptr, remaining, 0);
        if (sent == SOCKET_ERROR)
        {
            LeaveCriticalSection(&m_sendLock);
            return false;
        }
        ptr += sent;
        remaining -= static_cast<uint32_t>(sent);
    }

    LeaveCriticalSection(&m_sendLock);
    return true;
}

bool TcpServer::Receive(PacketHeader* outHeader, void* outPayload, uint32_t maxPayload)
{
    // Read header first
    if (!RecvExact(outHeader, sizeof(PacketHeader)))
        return false;

    // Read payload if any
    if (outHeader->length > 0)
    {
        if (outHeader->length > maxPayload)
            return false;  // payload too large for buffer

        if (!RecvExact(outPayload, outHeader->length))
            return false;
    }

    return true;
}

bool TcpServer::RecvExact(void* buf, uint32_t size)
{
    char* ptr = static_cast<char*>(buf);
    uint32_t remaining = size;

    while (remaining > 0)
    {
        int received = recv(m_clientSocket, ptr, remaining, 0);
        if (received <= 0)
            return false;  // disconnect or error
        ptr += received;
        remaining -= static_cast<uint32_t>(received);
    }

    return true;
}
