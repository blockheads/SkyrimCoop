#pragma once

#include "replay/replay_harness.h"
#include "server_replay_harness.h"
#include <string>

// DualReplayHarness: coordinator wrapping both client-side (hook event) and
// server-side (enet6 message) replay. Each side replays independently from
// its own recorded NDJSON file (D-06 from plan).
class DualReplayHarness
{
public:
    DualReplayHarness() = default;

    // Client-side: replay NDJSON hook events through native_client services
    size_t ReplayClientFile(const std::string& aPath);

    // Server-side: replay NDJSON enet6 messages through server services
    size_t ReplayServerFile(const std::string& aPath, TiltedPhoques::ConnectionId_t aPeer = 1);

    // Access for assertions
    ReplayHarness& Client() { return m_client; }
    ServerReplayHarness& Server() { return m_server; }

private:
    ReplayHarness m_client;
    ServerReplayHarness m_server;
};
