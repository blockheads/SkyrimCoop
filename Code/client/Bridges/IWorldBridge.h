#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Structs/TimeModel.h>

/// Sky::Get() and ModManager::Get() operations — weather and mod-list access. Every game's: what only the
/// host reads is HostEngine's, what only a guest applies is GuestEngine's.
/// No SKSE types in any signature.
class IWorldBridge
{
public:
    virtual ~IWorldBridge() = default;

    // --- Weather ---
    virtual uint32_t GetCurrentWeatherId() const noexcept = 0;

    // --- Mod registry ---
    virtual uint32_t GetModCount() const noexcept = 0;


    // --- Time ---
    /// Game time as hours (0.0 – 24.0).
    virtual float GetGameTimeHours() const noexcept = 0;
    /// Runs this game's clock no faster than aMultiplier (the other players' slowest; 1 lets it run as it wants).
    virtual void SetSharedTimeScale(float aMultiplier) noexcept = 0;

    /// Additional time data fields used by TransportService authentication.
    virtual float GetGameTimeScale() const noexcept = 0;
    virtual float GetGameYear() const noexcept = 0;
    virtual float GetGameMonth() const noexcept = 0;
    virtual float GetGameDay() const noexcept = 0;
    /// The whole calendar: speed, hour, date and whole days passed.
    virtual TimeModel GetGameTime() const noexcept = 0;
};
