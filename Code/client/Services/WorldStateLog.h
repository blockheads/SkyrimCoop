#pragma once

#include <Messages/WorldState.h>

#include <spdlog/fmt/fmt.h>

#include <string>

// World-state messages as one log line each (host sends, guest applies or proposes): what a scenario's failure
// digest shows of them.
namespace WorldStateLog
{
inline std::string Describe(const ObjectChange& acChange)
{
    const auto cObject = acChange.Object.LogFormat();
    switch (acChange.Type)
    {
    case ObjectChange::Kind::kActivate: return fmt::format("activate {:X} by {:X} (open state {})", cObject, acChange.Activator.LogFormat(), acChange.OpenState);
    case ObjectChange::Kind::kAnimation: return fmt::format("animation {:X} '{}' '{}'", cObject, acChange.Animation.c_str(), acChange.Event.c_str());
    case ObjectChange::Kind::kItems: return fmt::format("items {:X} holds {} of {:X}", cObject, acChange.Value, acChange.Item.LogFormat());
    case ObjectChange::Kind::kEnabled: return fmt::format("{} {:X}", acChange.Value ? "enable" : "disable", cObject);
    // ReferenceStates' packing: the level in the low byte, locked at 0x100.
    case ObjectChange::Kind::kLock:
        return fmt::format("lock {:X} {} at {}", cObject, (acChange.Value & 0x100) ? "locked" : "unlocked", acChange.Value & 0xFF);
    }
    return fmt::format("kind {} {:X}", static_cast<int>(acChange.Type), cObject);
}

// The first few, then how many more.
template <class T, class F> std::string List(const TiltedPhoques::Vector<T>& acItems, F aDescribe, size_t aMax = 8)
{
    std::string text;
    for (size_t i = 0; i < acItems.size() && i < aMax; ++i)
        text += (i ? "; " : "") + aDescribe(acItems[i]);
    if (acItems.size() > aMax)
        text += fmt::format("; and {} more", acItems.size() - aMax);
    return text;
}
} // namespace WorldStateLog
