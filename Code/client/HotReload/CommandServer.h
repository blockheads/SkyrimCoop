#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace CoopReload
{
// Test-harness command socket: line-delimited JSON on 127.0.0.1, served on the game thread every
// frame (scripts/coop_test.py). The port is written to <control dir>/rpc.json, so a request costs
// one frame instead of the control files' 250ms poll.
class CommandServer
{
public:
    // Answers one request line with one response line (no newline).
    using THandler = std::function<std::string(const std::string&)>;

    ~CommandServer();
    // Opens the socket and writes rpc.json into aDirectory; false if it could not.
    bool Start(const std::string& aDirectory, THandler aHandler);
    // Game thread, every frame: accepts, reads and answers without blocking.
    void Poll();

private:
    struct Client
    {
        uintptr_t Socket;
        std::string Inbound;
        std::string Outbound;
    };

    uintptr_t m_listener = ~uintptr_t(0);
    std::vector<Client> m_clients;
    THandler m_handler;
};
} // namespace CoopReload
