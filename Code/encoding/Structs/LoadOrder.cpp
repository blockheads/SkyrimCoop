#include <Structs/LoadOrder.h>

#include <algorithm>
#include <cctype>

namespace
{
std::string Lower(const TiltedPhoques::String& acName)
{
    std::string lower(acName.c_str());
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

const Mods::Entry* Find(const Mods& acMods, const std::string& acLower)
{
    const auto it = std::find_if(acMods.ModList.begin(), acMods.ModList.end(), [&](const Mods::Entry& acEntry) { return Lower(acEntry.Filename) == acLower; });
    return it == acMods.ModList.end() ? nullptr : &*it;
}
} // namespace

std::vector<LoadOrderDifference> CompareLoadOrders(const Mods& acHost, const Mods& acPlayer) noexcept
{
    using enum LoadOrderDifference::Kind;
    std::vector<LoadOrderDifference> differences;
    // Each side's plugins the other loads too, in its own order.
    std::vector<const Mods::Entry*> hostShared, playerShared;

    for (const auto& cEntry : acHost.ModList)
    {
        const auto* cpTheirs = Find(acPlayer, Lower(cEntry.Filename));
        if (!cpTheirs)
            differences.push_back({kMissing, cEntry.Filename.c_str()});
        else
        {
            hostShared.push_back(&cEntry);
            if (cpTheirs->IsLite != cEntry.IsLite)
                differences.push_back({kLight, cEntry.Filename.c_str(), {}, {}, cEntry.IsLite});
        }
    }
    for (const auto& cEntry : acPlayer.ModList)
    {
        if (!Find(acHost, Lower(cEntry.Filename)))
            differences.push_back({kExtra, cEntry.Filename.c_str()});
        else
            playerShared.push_back(&cEntry);
    }

    // Past the first place they part, every plugin is out of place: that one is what to fix.
    for (size_t i = 0; i < hostShared.size(); ++i)
    {
        if (Lower(hostShared[i]->Filename) == Lower(playerShared[i]->Filename))
            continue;
        differences.push_back({kOrder, hostShared[i]->Filename.c_str(), playerShared[i]->Filename.c_str(), i ? hostShared[i - 1]->Filename.c_str() : ""});
        break;
    }
    return differences;
}
