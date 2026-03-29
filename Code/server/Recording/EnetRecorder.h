#pragma once

#include <Messages/Message.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <TiltedCore/Stl.hpp>

using TiltedPhoques::ConnectionId_t;

// Records enet6 application-layer messages to per-peer NDJSON files.
// Per D-03: taps at GameServer::Send() and OnConsume(), NOT transport layer.
// Per D-04: one file per peer, both directions tagged with "dir" field.
class EnetRecorder
{
public:
    // aOutputDir: directory where per-peer NDJSON files are written
    explicit EnetRecorder(const std::string& aOutputDir);
    ~EnetRecorder();

    // Record an incoming ClientMessage (direction "c2s")
    void RecordClientMessage(ConnectionId_t aPeer, const ClientMessage& acMessage);
    // Record an outgoing ServerMessage (direction "s2c")
    void RecordServerMessage(ConnectionId_t aPeer, const ServerMessage& acMessage);

private:
    std::ofstream& GetPeerFile(ConnectionId_t aPeer);
    void WriteLine(ConnectionId_t aPeer, nlohmann::json& aJson, const char* apDir);

    std::string m_outputDir;
    std::unordered_map<ConnectionId_t, std::ofstream> m_peerFiles;
    std::chrono::steady_clock::time_point m_startTime;
    std::mutex m_mutex;
};
