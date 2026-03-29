#include "dual_replay_harness.h"

size_t DualReplayHarness::ReplayClientFile(const std::string& aPath)
{
    return m_client.ReplayFile(aPath);
}

size_t DualReplayHarness::ReplayServerFile(const std::string& aPath, TiltedPhoques::ConnectionId_t aPeer)
{
    return m_server.ReplayFile(aPath, aPeer);
}
