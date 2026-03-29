#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <GameServer.h>
#include <console/ConsoleRegistry.h>

// ServerReplayHarness: replays NDJSON-recorded enet6 traffic through the server
// service stack without enet6 port binding, ESLoader, or Skyrim.
//
// NDJSON format (per-peer files from EnetRecorder):
//   {"type":"AuthenticationRequest","opcode":0,"payload":"base64...","dir":"c2s","peer":1,"ts":0.001}
//   {"type":"AuthenticationResponse","opcode":0,"payload":"base64...","dir":"s2c","peer":1,"ts":0.002}
//
// Only "c2s" lines are injected into the server. "s2c" lines are skipped.
// Outgoing ServerMessages are captured via loopback route for assertions.
class ServerReplayHarness
{
public:
    ServerReplayHarness();
    ~ServerReplayHarness();

    // Replay a per-peer NDJSON file containing c2s messages.
    // Injects each c2s line as a ClientMessage into the server.
    // Returns number of messages injected.
    size_t ReplayFile(const std::string& aPath, TiltedPhoques::ConnectionId_t aPeer = 1);

    // Access for assertions
    Server::World& GetWorld();
    Server::GameServer& GetGameServer();
    std::vector<nlohmann::json>& GetCapturedServerMessages();

private:
    ServerConsole::ConsoleRegistry m_console;
    std::unique_ptr<Server::GameServer> m_pServer;
    std::vector<nlohmann::json> m_captured;
};
