#pragma once

#include "ReplyQueue.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace TestHarness
{
// Test-harness command socket: line-delimited JSON on 127.0.0.1, served on the game thread every frame
// (scripts/coop_test.py); a large reply is finished on a worker, all sent in order. The port is written to
// <control dir>/rpc.json, so a request costs one frame instead of the control files' 250ms poll.
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
    THandler m_handler;
};
} // namespace TestHarness
