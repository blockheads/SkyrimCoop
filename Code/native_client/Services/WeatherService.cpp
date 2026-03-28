#include "WeatherService.h"

#include <spdlog/spdlog.h>

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"
#include "protocol.h"

// Game struct offsets (extracted from existing game headers, no direct includes)
// Sky singleton: accessed via Address Library ID 13878, stored as global pointer
// Sky::currentWeather at offset 0x1C8 (pointer to TESWeather)
// TESForm::formID at offset 0x14
static constexpr uint64_t cSkyWeatherOffset = 0x1C8;
static constexpr uint64_t cFormIdOffset = 0x14;

WeatherService::WeatherService(GameReader& aReader, TcpClient& aClient)
    : m_reader(aReader)
    , m_client(aClient)
{
    spdlog::info("WeatherService initialized");
}

void WeatherService::OnHookEvent(const HookEventPacket& aPkt)
{
    switch (static_cast<HookOpcode>(aPkt.header.opcode)) {
    case HOOK_SET_WEATHER: {
        // args[0] = Sky*, args[1] = TESWeather*, args[2] = override flag
        if (aPkt.argCount < 2) break;
        uint64_t weatherPtr = aPkt.args[1];
        if (weatherPtr == 0) break;

        uint32_t formId = m_reader.ReadFormId(weatherPtr);
        if (formId != 0 && formId != cMapWeatherId) {
            spdlog::info("WeatherService: SetWeather hook, formId={:#x}", formId);
            m_cachedWeatherId = formId;
        }
        break;
    }
    case HOOK_FORCE_WEATHER: {
        // args[0] = Sky*, args[1] = TESWeather*, args[2] = override flag
        if (aPkt.argCount < 2) break;
        uint64_t weatherPtr = aPkt.args[1];
        if (weatherPtr == 0) break;

        uint32_t formId = m_reader.ReadFormId(weatherPtr);
        if (formId != 0 && formId != cMapWeatherId) {
            spdlog::info("WeatherService: ForceWeather hook, formId={:#x}", formId);
            m_cachedWeatherId = formId;
        }
        break;
    }
    case HOOK_WEATHER_CHANGE: {
        // UpdateWeather hook -- just note it, poll will handle state
        spdlog::debug("WeatherService: weather update tick");
        break;
    }
    default:
        break;
    }
}

void WeatherService::Update(float aDeltaTime)
{
    m_pollTimer += aDeltaTime;
    if (m_pollTimer < cPollInterval)
        return;
    m_pollTimer = 0.0f;

    uint32_t currentId = ReadCurrentWeatherId();
    if (currentId == 0 || currentId == cMapWeatherId)
        return;

    if (currentId != m_cachedWeatherId) {
        m_cachedWeatherId = currentId;
        // TODO(Plan 07+): Send RequestWeatherChange via network transport
        // For now, log the weather change event
        spdlog::info("WeatherService: weather changed to formId={:#x}", currentId);
    }
}

uint32_t WeatherService::ReadCurrentWeatherId()
{
    // Sky singleton pointer is stored in the pointer table (resolved from Address Library)
    // For now we use a known global -- the pointer table will be populated by the
    // actor tracking system. Sky pointer lookup requires the Address Library ID 13878.
    // This is a placeholder that will be wired when the global pointer table is extended.

    // Try to read the Sky pointer from the pointer table using a well-known formId
    // Sky is not a TESForm, so we use a special sentinel (0xFFFF0001)
    uint64_t skyPtr = m_reader.LookupPointer(0xFFFF0001);
    if (skyPtr == 0) {
        // Sky pointer not yet in table -- services need global pointer resolution
        // which will be added in Plan 06+
        return 0;
    }

    // Read: Sky->currentWeather (pointer), then TESWeather->formID
    return m_reader.ReadChain<uint32_t>(skyPtr, cSkyWeatherOffset, cFormIdOffset);
}
