#pragma once
#include <cstdint>

class GameReader;
struct HookEventPacket;
class TcpClient;

// QuestService: Receives quest-related hook events and logs them.
// Network wiring is deferred to Plan 07+ when the full TransportService
// is available in the native process. Quest sync was optional in the
// original codebase and is not on the critical path.
class QuestService {
public:
    QuestService(GameReader& aReader, TcpClient& aClient);

    // Dispatch hook events from DLL
    void OnHookEvent(const HookEventPacket& aPkt);

    // Periodic update (minimal -- quest state is event-driven)
    void Update(float aDeltaTime);

private:
    GameReader& m_reader;
    TcpClient& m_client;
    uint32_t m_lastQuestEventCount = 0;
};
