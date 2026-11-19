#pragma once

#include "ReplyQueue.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace TestHarness
{
// Test-harness command socket (scripts/coop_test.py): line-delimited JSON on 127.0.0.1, served on the game thread
// every frame, a large reply finished on a worker (docs/re/systems/harness-cost.md); the port in <control dir>/rpc.json.
class CommandServer
{
public:
    // Takes one request line on the game thread: its response line (no newline), or what makes it off that thread.
    struct Reply
    {
        std::string Ready;
        ReplyQueue::TFinish Later;
    };
    using THandler = std::function<Reply(const std::string&)>;

    ~CommandServer();
    // Opens the socket and writes rpc.json into aDirectory; false if it could not.
    bool Start(const std::string& aDirectory, THandler aHandler);
    // Game thread, every frame: accepts, reads, starts requests and sends finished replies without blocking.
    void Poll();
    // Closes the socket and every connection (the plugin unloads).
    void Stop();

private:
    struct Client
    {
        uintptr_t Socket;
        std::string Inbound;
        ReplyQueue Replies;
        std::string Outbound;
    };

    uintptr_t m_listener = ~uintptr_t(0);
    std::vector<Client> m_clients;
    // Closed clients' replies still being built: dropped once finished, never waited for in Poll.
    std::vector<ReplyQueue> m_closed;
    THandler m_handler;
};
} // namespace TestHarness
