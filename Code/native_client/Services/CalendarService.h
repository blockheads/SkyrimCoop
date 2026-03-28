#pragma once
#include <cstdint>

class GameReader;
struct HookEventPacket;
class TcpClient;

// CalendarService: Reads game time via GameReader (/proc/pid/mem)
// and syncs time state with other players.
// Time is read from the game's global time variables via known memory offsets.
class CalendarService {
public:
    CalendarService(GameReader& aReader, TcpClient& aClient);

    // Dispatch hook events from DLL
    void OnHookEvent(const HookEventPacket& aPkt);

    // Periodic update: read game time from memory
    void Update(float aDeltaTime);

    float GetGameHour() const { return m_gameHour; }
    float GetGameDay() const { return m_gameDay; }

private:
    GameReader& m_reader;
    TcpClient& m_client;

    float m_gameHour = 0.0f;
    float m_gameDay = 0.0f;
    float m_gameMonth = 0.0f;
    float m_gameYear = 0.0f;
    float m_timeScale = 20.0f;  // default Skyrim time scale
    float m_pollTimer = 0.0f;

    // Poll interval (1 second is fine for calendar -- not latency-sensitive)
    static constexpr float cPollInterval = 1.0f;
};
