#pragma once

#include <Structs/Mods.h>

#include <string>
#include <vector>

// How a player's plugins differ from the host's (docs/design/join-flow.md, stage 3): the join gate
// refuses any difference, and the refused player is told each one. Names compare without case.
struct LoadOrderDifference
{
    enum class Kind
    {
        kMissing, // the host loads Plugin, the player does not
        kExtra,   // the player loads Plugin, the host does not
        kLight,   // both load Plugin, one of them as a light plugin (Light: the host's)
        kOrder,   // the first place the plugins both load are in another order: after After, the
                  // host loads Plugin and the player Theirs
    };

    Kind What{};
    std::string Plugin;
    std::string Theirs;
    std::string After;
    bool Light{};

    bool operator==(const LoadOrderDifference&) const = default;
};

std::vector<LoadOrderDifference> CompareLoadOrders(const Mods& acHost, const Mods& acPlayer) noexcept;
