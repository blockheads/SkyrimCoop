#include "WeatherService.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "json_protocol.h"

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"

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

void WeatherService::OnHookEvent(const nlohmann::json& aMsg)
{
    const auto& op = aMsg[JsonField::kOp].get_ref<const std::string&>();

    if (op == JsonOp::kWeatherChange) {
        // Periodic weather update -- sky_ptr only, read current weather via memory
        uint64_t skyPtr = aMsg.value(JsonField::kSkyPtr, 0ull);
        if (skyPtr != 0) {
            // Store sky pointer in pointer table for future lookups
            m_reader.GetPointers().Insert(0xFFFF0001, skyPtr);
        }
    } else if (op == JsonOp::kSetWeather) {
        uint32_t weatherFormId = aMsg.value(JsonField::kWeatherFormId, 0u);
        bool override_ = aMsg.value(JsonField::kOverride, false);
        bool accel = aMsg.value(JsonField::kAccel, false);
        spdlog::info("WeatherService: set_weather formId={:#x} override={} accel={}",
                     weatherFormId, override_, accel);
        if (weatherFormId != 0 && weatherFormId != cMapWeatherId) {
            m_cachedWeatherId = weatherFormId;
        }
    } else if (op == JsonOp::kForceWeather) {
        uint32_t weatherFormId = aMsg.value(JsonField::kWeatherFormId, 0u);
        bool override_ = aMsg.value(JsonField::kOverride, false);
        spdlog::info("WeatherService: force_weather formId={:#x} override={}",
                     weatherFormId, override_);
        if (weatherFormId != 0 && weatherFormId != cMapWeatherId) {
            m_cachedWeatherId = weatherFormId;
        }
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

void WeatherService::SendForceWeather(uint32_t aWeatherFormId)
{
    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdForceWeather},
        {JsonField::kWeatherFormId, aWeatherFormId}
    };
    m_client.SendJsonLine(cmd);
    spdlog::debug("WeatherService: sent cmd_force_weather formId={:#x}", aWeatherFormId);
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
