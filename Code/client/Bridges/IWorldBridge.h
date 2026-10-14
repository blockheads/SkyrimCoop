#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Sky::Get() and ModManager::Get() operations — weather and mod-list access.
/// No SKSE types in any signature.
class IWorldBridge
{
public:
    virtual ~IWorldBridge() = default;

    // --- Weather ---
    virtual uint32_t GetCurrentWeatherId() const noexcept = 0;
    virtual uint32_t GetLastWeatherId() const noexcept = 0;
    virtual void ForceWeather(uint32_t aWeatherFormId, bool aOverride) noexcept = 0;
    virtual void ReleaseWeatherOverride() noexcept = 0;

    // --- Mod registry ---
    virtual uint32_t GetModCount() const noexcept = 0;
    virtual std::string GetModName(uint32_t aIndex) const noexcept = 0;
    virtual std::vector<std::string> GetAllModNames() const noexcept = 0;

    // --- Toggle game systems ---
    /// Enable or disable the game's own weather update system
    /// (WeatherService disables it when the server controls weather).
    virtual void SetGameWeatherSystemEnabled(bool aEnable) noexcept = 0;

    // --- Time ---
    /// Get/set game time as hours (0.0 – 24.0).
    virtual float GetGameTimeHours() const noexcept = 0;
    virtual void SetGameTimeHours(float aHours) noexcept = 0;
    /// Runs this game's clock no faster than aMultiplier (the other players' slowest; 1 lets it run as it wants).
    virtual void SetSharedTimeScale(float aMultiplier) noexcept = 0;

    /// Additional time data fields used by TransportService authentication.
    virtual float GetGameTimeScale() const noexcept = 0;
    virtual float GetGameYear() const noexcept = 0;
    virtual float GetGameMonth() const noexcept = 0;
    virtual float GetGameDay() const noexcept = 0;
};
