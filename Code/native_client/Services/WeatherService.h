#pragma once
#include <cstdint>

#include <nlohmann/json_fwd.hpp>

class GameReader;
class TcpClient;

// WeatherService: Reads weather state via GameReader (/proc/pid/mem)
// and syncs weather changes over the network.
// Receives hook events: HOOK_SET_WEATHER, HOOK_FORCE_WEATHER, HOOK_WEATHER_CHANGE
// Sends commands: cmd_force_weather (JSON)
class WeatherService {
public:
    WeatherService(GameReader& aReader, TcpClient& aClient);

    // Dispatch hook events from DLL
    void OnHookEvent(const nlohmann::json& aMsg);

    // Periodic update: poll weather state from game memory
    void Update(float aDeltaTime);

    // Send a cmd_force_weather command to the DLL
    void SendForceWeather(uint32_t aWeatherFormId);

private:
    // Read current weather formId from Sky singleton via memory
    uint32_t ReadCurrentWeatherId();

    GameReader& m_reader;
    TcpClient& m_client;
    uint32_t m_cachedWeatherId = 0;
    float m_pollTimer = 0.0f;

    // Map weather formId that should not be synced
    static constexpr uint32_t cMapWeatherId = 0xA6858;
    // Poll interval in seconds (matches original 250ms update)
    static constexpr float cPollInterval = 0.25f;
};
