// Relay protocol integration tests (D-02, D-04 step 3)
// Tests TcpClient against a mock TCP server over real TCP sockets.
// Updated for Phase 11 JSON wire protocol: tests NDJSON send/receive.

#include <catch2/catch.hpp>
#include "tcp_client.h"
#include "json_protocol.h"
#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cstring>

using json = nlohmann::json;

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

    // Send an NDJSON line (JSON string + '\n')
    bool SendNdjsonLine(const json& aMsg)
    {
        std::string line = aMsg.dump() + "\n";
        const char* pBuf = line.c_str();
        size_t remaining = line.size();
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

    // Receive an NDJSON line from the client (reads until '\n')
    bool RecvNdjsonLine(json& aOut)
    {
        std::string line;
        char ch;
        while (true)
        {
            ssize_t n = ::recv(m_clientFd, &ch, 1, 0);
            if (n <= 0)
                return false;
            if (ch == '\n')
                break;
            line += ch;
        }
        if (line.empty())
            return false;
        aOut = json::parse(line);
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

TEST_CASE("Relay_JsonHookEvent_RoundTrip", "[RelayIntegration][JsonProtocol]")
{
    MockTcpServer server;

    std::thread acceptThread([&server]() {
        server.Accept();
    });

    TcpClient client;
    REQUIRE(client.Connect(server.GetPort()));
    acceptThread.join();

    // Mock DLL side: send an actor_added NDJSON event
    json hookMsg;
    hookMsg[JsonField::kOp] = JsonOp::kActorAdded;
    hookMsg[JsonField::kPtr] = 0xDEADBEEF12345678ULL;
    hookMsg[JsonField::kFormId] = 0x14u;

    REQUIRE(server.SendNdjsonLine(hookMsg));

    // TcpClient side: receive the JSON line
    json received;
    REQUIRE(client.ReceiveJsonLine(received));

    REQUIRE(received[JsonField::kOp] == JsonOp::kActorAdded);
    REQUIRE(received[JsonField::kFormId] == 0x14u);
    REQUIRE(received[JsonField::kPtr] == 0xDEADBEEF12345678ULL);
}

TEST_CASE("Relay_JsonCommand_RoundTrip", "[RelayIntegration][JsonProtocol]")
{
    MockTcpServer server;

    std::thread acceptThread([&server]() {
        server.Accept();
    });

    TcpClient client;
    REQUIRE(client.Connect(server.GetPort()));
    acceptThread.join();

    // TcpClient sends a cmd_set_position JSON command
    json cmdMsg;
    cmdMsg[JsonField::kOp] = JsonOp::kCmdSetPosition;
    cmdMsg[JsonField::kActorPtr] = 0xCAFEBABEull;
    cmdMsg[JsonField::kX] = 100.5f;
    cmdMsg[JsonField::kY] = 200.25f;
    cmdMsg[JsonField::kZ] = 300.75f;

    REQUIRE(client.SendJsonLine(cmdMsg));

    // Mock DLL side: receive and verify JSON
    json recvMsg;
    REQUIRE(server.RecvNdjsonLine(recvMsg));

    REQUIRE(recvMsg[JsonField::kOp] == JsonOp::kCmdSetPosition);
    REQUIRE(recvMsg[JsonField::kActorPtr] == 0xCAFEBABEull);
    REQUIRE(recvMsg[JsonField::kX].get<float>() == Approx(100.5f));
    REQUIRE(recvMsg[JsonField::kY].get<float>() == Approx(200.25f));
    REQUIRE(recvMsg[JsonField::kZ].get<float>() == Approx(300.75f));
}

TEST_CASE("Relay_JsonControlPacket_Ready", "[RelayIntegration][JsonProtocol]")
{
    MockTcpServer server;

    std::thread acceptThread([&server]() {
        server.Accept();
    });

    TcpClient client;
    REQUIRE(client.Connect(server.GetPort()));
    acceptThread.join();

    // TcpClient sends ctrl_ready
    REQUIRE(client.SendControlJson(JsonOp::kCtrlReady));

    // Mock DLL side: receive and verify
    json recvMsg;
    REQUIRE(server.RecvNdjsonLine(recvMsg));

    REQUIRE(recvMsg[JsonField::kOp] == JsonOp::kCtrlReady);
    REQUIRE(recvMsg.size() == 1); // only "op" field
}
