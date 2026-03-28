// Relay protocol integration tests (D-02, D-04 step 3)
// Tests TcpClient against a mock TCP server over real TCP sockets.
// Unlike relay_protocol_tests.cpp (which tests structs via memcpy),
// these tests exercise the actual TCP send/receive path.

#include <catch2/catch.hpp>
#include "tcp_client.h"
#include "protocol.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cstring>

// MockTcpServer: simple POSIX TCP listener for testing TcpClient
struct MockTcpServer
{
    int m_listenFd{-1};
    int m_clientFd{-1};
    uint16_t m_port{0};

    MockTcpServer()
    {
        m_listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(m_listenFd >= 0);

        int optval = 1;
        ::setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = 0; // OS-assigned port
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        REQUIRE(::bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);

        socklen_t addrLen = sizeof(addr);
        ::getsockname(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
        m_port = ntohs(addr.sin_port);

        REQUIRE(::listen(m_listenFd, 1) == 0);
    }

    ~MockTcpServer()
    {
        if (m_clientFd >= 0)
            ::close(m_clientFd);
        if (m_listenFd >= 0)
            ::close(m_listenFd);
    }

    uint16_t GetPort() const { return m_port; }

    // Accept one connection. Blocks with a 5-second timeout.
    bool Accept()
    {
        struct timeval tv{};
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        ::setsockopt(m_listenFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        m_clientFd = ::accept(m_listenFd, nullptr, nullptr);
        return m_clientFd >= 0;
    }

    // Send raw bytes on the accepted connection
    bool SendRaw(const void* aData, size_t aSize)
    {
        const uint8_t* pBuf = static_cast<const uint8_t*>(aData);
        size_t remaining = aSize;
        while (remaining > 0)
        {
            ssize_t sent = ::send(m_clientFd, pBuf, remaining, MSG_NOSIGNAL);
            if (sent <= 0)
                return false;
            pBuf += sent;
            remaining -= static_cast<size_t>(sent);
        }
        return true;
    }

    // Receive raw bytes from the accepted connection
    bool RecvRaw(void* aData, size_t aSize)
    {
        uint8_t* pBuf = static_cast<uint8_t*>(aData);
        size_t remaining = aSize;
        while (remaining > 0)
        {
            ssize_t received = ::recv(m_clientFd, pBuf, remaining, 0);
            if (received <= 0)
                return false;
            pBuf += received;
            remaining -= static_cast<size_t>(received);
        }
        return true;
    }
};

TEST_CASE("Relay_TcpClient_ConnectsToMockDll", "[RelayIntegration]")
{
    MockTcpServer server;

    // Accept in a thread since TcpClient::Connect blocks until connected
    std::thread acceptThread([&server]() {
        server.Accept();
    });

    TcpClient client;
    REQUIRE(client.Connect(server.GetPort()));
    REQUIRE(client.IsConnected());

    acceptThread.join();
    REQUIRE(server.m_clientFd >= 0);
}

TEST_CASE("Relay_HookEvent_RoundTrip", "[RelayIntegration]")
{
    MockTcpServer server;

    std::thread acceptThread([&server]() {
        server.Accept();
    });

    TcpClient client;
    REQUIRE(client.Connect(server.GetPort()));
    acceptThread.join();

    // Mock DLL side: send a HookEventPacket
    HookEventPacket hookPkt{};
    hookPkt.header.opcode = HOOK_ACTOR_ADDED;
    hookPkt.header.length = sizeof(HookEventPacket) - sizeof(PacketHeader);
    hookPkt.argCount = 2;
    hookPkt.args[0] = 0xDEADBEEF12345678ULL;
    hookPkt.args[1] = 0x00000014ULL; // formID

    REQUIRE(server.SendRaw(&hookPkt, sizeof(hookPkt)));

    // TcpClient side: receive the packet
    // Receive() reads header separately, then payload = everything after header
    PacketHeader recvHeader{};
    uint8_t payloadBuf[256]{};
    uint16_t expectedPayloadLen = sizeof(HookEventPacket) - sizeof(PacketHeader);

    REQUIRE(client.Receive(&recvHeader, payloadBuf, sizeof(payloadBuf)));

    REQUIRE(recvHeader.opcode == HOOK_ACTOR_ADDED);
    REQUIRE(recvHeader.length == expectedPayloadLen);

    // Reconstruct full HookEventPacket by combining header + payload
    HookEventPacket recvPkt{};
    recvPkt.header = recvHeader;
    std::memcpy(reinterpret_cast<uint8_t*>(&recvPkt) + sizeof(PacketHeader),
                payloadBuf, expectedPayloadLen);

    REQUIRE(recvPkt.argCount == 2);
    REQUIRE(recvPkt.args[0] == 0xDEADBEEF12345678ULL);
    REQUIRE(recvPkt.args[1] == 0x00000014ULL);
}

TEST_CASE("Relay_Command_RoundTrip", "[RelayIntegration]")
{
    MockTcpServer server;

    std::thread acceptThread([&server]() {
        server.Accept();
    });

    TcpClient client;
    REQUIRE(client.Connect(server.GetPort()));
    acceptThread.join();

    // TcpClient sends a CommandPacket
    CommandPacket cmdPkt{};
    cmdPkt.header.opcode = CMD_SET_POSITION;
    cmdPkt.header.length = sizeof(CommandPacket) - sizeof(PacketHeader);
    cmdPkt.argCount = 3;
    cmdPkt.args[0] = 0x00000001ULL;    // actorFormID
    cmdPkt.args[1] = 0x4048F5C3ULL;    // x as bits
    cmdPkt.args[2] = 0x40590000ULL;    // y as bits

    REQUIRE(client.Send(&cmdPkt, sizeof(cmdPkt)));

    // Mock DLL side: receive and verify
    CommandPacket recvPkt{};
    REQUIRE(server.RecvRaw(&recvPkt, sizeof(recvPkt)));

    REQUIRE(recvPkt.header.opcode == CMD_SET_POSITION);
    REQUIRE(recvPkt.header.length == sizeof(CommandPacket) - sizeof(PacketHeader));
    REQUIRE(recvPkt.argCount == 3);
    REQUIRE(recvPkt.args[0] == 0x00000001ULL);
    REQUIRE(recvPkt.args[1] == 0x4048F5C3ULL);
    REQUIRE(recvPkt.args[2] == 0x40590000ULL);
}

TEST_CASE("Relay_ControlPacket_Ready", "[RelayIntegration]")
{
    MockTcpServer server;

    std::thread acceptThread([&server]() {
        server.Accept();
    });

    TcpClient client;
    REQUIRE(client.Connect(server.GetPort()));
    acceptThread.join();

    // TcpClient sends CTRL_READY
    REQUIRE(client.SendControl(CTRL_READY));

    // Mock DLL side: receive and verify
    PacketHeader recvHeader{};
    REQUIRE(server.RecvRaw(&recvHeader, sizeof(recvHeader)));

    REQUIRE(recvHeader.opcode == CTRL_READY);
    REQUIRE(recvHeader.length == 0);
}
