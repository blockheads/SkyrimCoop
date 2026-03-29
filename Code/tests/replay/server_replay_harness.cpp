#include "server_replay_harness.h"
#include <Recording/MessageSerializer.h>
#include <Messages/ServerMessageFactory.h>
#include <fstream>
#include <spdlog/spdlog.h>

ServerReplayHarness::ServerReplayHarness()
    : m_console("ReplayTest")
{
    // Create server in test mode: no enet6 port binding, no ESLoader, no ScriptService
    m_pServer = std::make_unique<Server::GameServer>(m_console, GameServerTestMode{});

    // Set up loopback route to capture outgoing ServerMessages as JSON.
    // Send() writes: [packet_type_byte=0][opcode][payload...]
    // ServerMessageFactory::Extract expects: [opcode][payload...]
    // So we skip the first byte before calling Extract().
    m_pServer->SetLoopbackRoute(1, [this](const void* apData, uint32_t aSize) {
        if (aSize < 2)
            return;

        // Skip the packet type byte (byte 0)
        const auto* pPayload = static_cast<const uint8_t*>(apData) + 1;
        const uint32_t cPayloadSize = aSize - 1;

        ViewBuffer buf(const_cast<uint8_t*>(pPayload), cPayloadSize);
        Buffer::Reader reader(&buf);
        ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);
        if (pMessage)
            m_captured.push_back(MessageSerializer::ServerToJson(*pMessage));
        else
            spdlog::warn("ServerReplayHarness: failed to parse captured ServerMessage");
    });
}

ServerReplayHarness::~ServerReplayHarness()
{
    // Reset server before console is destroyed (console is a member, destroyed after m_pServer)
    m_pServer.reset();
}

size_t ServerReplayHarness::ReplayFile(const std::string& aPath, TiltedPhoques::ConnectionId_t aPeer)
{
    std::ifstream file(aPath);
    if (!file.is_open())
    {
        spdlog::error("ServerReplayHarness: failed to open {}", aPath);
        return 0;
    }

    size_t count = 0;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(line);
        }
        catch (const nlohmann::json::parse_error& e)
        {
            spdlog::warn("ServerReplayHarness: skipping malformed line: {}", e.what());
            continue;
        }

        // Only inject client-to-server messages
        if (!j.contains("dir") || j["dir"] != "c2s")
            continue;

        // Convert JSON back to binary for InjectMessage
        auto binary = MessageSerializer::ClientFromJson(j);
        if (binary.empty())
        {
            spdlog::warn("ServerReplayHarness: ClientFromJson returned empty for opcode {}",
                         j.value("opcode", -1));
            continue;
        }

        m_pServer->InjectMessage(binary.data(), static_cast<uint32_t>(binary.size()), aPeer);
        ++count;
    }

    spdlog::info("ServerReplayHarness: replayed {} c2s messages from {}", count, aPath);
    return count;
}

Server::World& ServerReplayHarness::GetWorld()
{
    return m_pServer->GetWorld();
}

Server::GameServer& ServerReplayHarness::GetGameServer()
{
    return *m_pServer;
}

std::vector<nlohmann::json>& ServerReplayHarness::GetCapturedServerMessages()
{
    return m_captured;
}
