#pragma once

#include <Forms/TESForm.h>

struct TESWeather;
struct TESClimate;

// A region of a worldspace; its weather list is in its data (docs/re/systems/weather.md).
struct TESRegion : TESForm
{
    // Its list holds the weather with a chance (16450).
    [[nodiscard]] bool HasWeather(const TESWeather* apWeather) const noexcept;
    // Its own weather after Sky's changed: aShown when its list holds it, else a roll of its list (16448).
    void SetCurrentWeather(TESWeather* apShown) noexcept;

    void* pDataList;            // 20
    void* pPointLists;          // 28
    void* pWorldSpace;          // 30
    TESWeather* pCurrentWeather; // 38
    uint8_t unk40[0x50 - 0x40];
};

static_assert(offsetof(TESRegion, pCurrentWeather) == 0x38);
static_assert(sizeof(TESRegion) == 0x50);

struct Sky
{
    // The world map's own weather while it is open (0xA6858).
    static constexpr uint32_t kWorldMapWeather = 0xA6858;

    static Sky* Get() noexcept;

    static bool s_shouldUpdateWeather;
    // A guest's held weather (GuestEngine::HoldWeather): its override never ages out, refreshed as 26231 starts.
    static inline TESWeather* s_pHeldWeather = nullptr;
    // The weather a script or the console set (26241, 26243: their only callers but the map's), until the player's
    // weather region is read again (those calls clear it).
    static inline TESWeather* s_pSetWeather = nullptr;

    virtual ~Sky();

    void SetWeather(TESWeather* apWeather) noexcept;
    // The engine's fade into it (26241); aOverride: a forced weather the regions do not change.
    void FadeTo(TESWeather* apWeather, bool aOverride) noexcept;
    void ForceWeather(TESWeather* apWeather) noexcept;
    void ReleaseWeatherOverride() noexcept;
    void ResetWeather() noexcept;

    TESWeather* GetWeather() const noexcept;
    // The region whose list Sky::UpdateWeather (26231) rolls: an interior's sky region, else the player's.
    [[nodiscard]] static TESRegion* PlayerWeatherRegion() noexcept;

    uint8_t unk8[0x40 - 0x8];
    TESClimate* pClimate;          // 40
    TESWeather* pCurrentWeather;   // 48
    TESWeather* pLastWeather;      // 50 the one fading out
    TESWeather* pDefaultWeather;   // 58
    TESWeather* pOverrideWeather;  // 60 a script's or the console's
    TESRegion* pRegion;            // 68 the region the last roll used
    uint8_t unk70[0x1B0 - 0x70];
    float currentGameHour;         // 1B0
    float lastWeatherUpdate;       // 1B4
    float currentWeatherPct;       // 1B8 the fade's progress
    uint8_t unk1BC[0x2C8 - 0x1BC];
};

inline bool Sky::s_shouldUpdateWeather = true;

static_assert(offsetof(Sky, pClimate) == 0x40);
static_assert(offsetof(Sky, pCurrentWeather) == 0x48);
static_assert(offsetof(Sky, pOverrideWeather) == 0x60);
static_assert(offsetof(Sky, pRegion) == 0x68);
static_assert(offsetof(Sky, currentGameHour) == 0x1B0);
static_assert(offsetof(Sky, currentWeatherPct) == 0x1B8);
static_assert(sizeof(Sky) == 0x2C8);
