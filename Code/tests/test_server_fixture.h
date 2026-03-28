#pragma once

// Shared GameServer fixture for network integration and round-trip tests.
// GameServer uses a static singleton (s_pInstance) and ConsoleRegistry consumes
// static Setting/Command lists on first construction. Both are one-per-process,
// so all test files share a single server instance via this header.

#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <ConsoleRegistry.h>
#include <GameServer.h>

namespace TestFixture
{

// RAII wrapper that ensures proper cleanup order to avoid "pure virtual method called"
// during static destruction. Server::Close() is called before the GameServer destructor
// runs, preventing enet callbacks from reaching a partially-destroyed vtable.
struct SharedServerHolder
{
    std::unique_ptr<ServerConsole::ConsoleRegistry> pConsole;
    std::unique_ptr<Server::GameServer> pServer;

    SharedServerHolder()
    {
        if (!spdlog::get("ConOut"))
            spdlog::stdout_color_mt("ConOut");

        pConsole = std::make_unique<ServerConsole::ConsoleRegistry>("ConOut");
        pServer = std::make_unique<Server::GameServer>(*pConsole);
        pServer->Initialize();
    }

    ~SharedServerHolder()
    {
        if (pServer)
        {
            pServer->Kill();
            pServer->Close();
        }
    }
};

inline Server::GameServer& GetSharedServer()
{
    static SharedServerHolder s_holder;
    return *s_holder.pServer;
}

} // namespace TestFixture
