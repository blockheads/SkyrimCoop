#pragma once

#include <Messages/WorldState.h>

#include <format>

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
    case ObjectChange::Kind::kActivate: return std::format("activate {:X} by {:X} (open state {})", cObject, acChange.Activator.LogFormat(), acChange.OpenState);
    case ObjectChange::Kind::kAnimation: return std::format("animation {:X} '{}' '{}'", cObject, acChange.Animation.c_str(), acChange.Event.c_str());
    case ObjectChange::Kind::kItems: return std::format("items {:X} holds {} of {:X}", cObject, acChange.Value, acChange.Item.LogFormat());
    // ReferenceStates' packing: 1 enabled, 2 deleted.
    case ObjectChange::Kind::kEnabled:
        return std::format("{} {:X}", (acChange.Value & 2) ? "delete" : acChange.Value ? "enable" : "disable", cObject);
    // ReferenceStates' packing: the level in the low byte, locked at 0x100.
    case ObjectChange::Kind::kLock:
        return std::format("lock {:X} {} at {}", cObject, (acChange.Value & 0x100) ? "locked" : "unlocked", acChange.Value & 0xFF);
    case ObjectChange::Kind::kOpenState: return std::format("{} {:X}", acChange.Value ? "open" : "close", cObject);
    case ObjectChange::Kind::kMove:
        if (acChange.Data.size() == 6 * sizeof(float))
        {
            float values[6];
            std::memcpy(values, acChange.Data.data(), sizeof(values));
            return std::format("move {:X} to ({:.0f}, {:.0f}, {:.0f}) angle ({:.2f}, {:.2f}, {:.2f})", cObject, values[0], values[1], values[2], values[3], values[4], values[5]);
        }
        return std::format("move {:X}", cObject);
    case ObjectChange::Kind::kScale: return std::format("scale {:X} {}%", cObject, acChange.Value);
    case ObjectChange::Kind::kOwner: return std::format("owner {:X} {:X}", cObject, acChange.Item.LogFormat());
    case ObjectChange::Kind::kCreate: return std::format("create {:X} of {:X}", cObject, acChange.Item.LogFormat());
    case ObjectChange::Kind::kRecord: return std::format("record {:X} {:X}", cObject, acChange.Value);
    case ObjectChange::Kind::kGraph: return std::format("graph {:X}: {} bytes", cObject, acChange.Data.size());
    case ObjectChange::Kind::kBase: return std::format("base {:X} {:X}", cObject, acChange.Item.LogFormat());
    case ObjectChange::Kind::kContents:
        return std::format("contents {:X}: {} items", cObject, ObjectChange::UnpackContents(acChange.Data).size());
    case ObjectChange::Kind::kSaveData: return std::format("save data {:X} flags {:X} ({} bytes)", cObject, acChange.Value, acChange.Data.size());
    }
    return std::format("kind {} {:X}", static_cast<int>(acChange.Type), cObject);
}

// The first few, then how many more.
template <class T, class F> std::string List(const std::vector<T>& acItems, F aDescribe, size_t aMax = 8)
{
    std::string text;
    for (size_t i = 0; i < acItems.size() && i < aMax; ++i)
        text += (i ? "; " : "") + aDescribe(acItems[i]);
    if (acItems.size() > aMax)
        text += std::format("; and {} more", acItems.size() - aMax);
    return text;
}
} // namespace WorldStateLog
