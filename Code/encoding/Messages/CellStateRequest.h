#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <Wire/Buffer.h>

// A reference whose change record holds these flags (TESObjectREFR::ChangeFlags) in the sender's game.
struct ChangedReference
{
    GameId Id{};
    uint32_t Flags{};

    bool operator==(const ChangedReference&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id, Flags); }
};

// A guest's cell finished loading: it asks for the host's state of the references there (world-state sync, step 2).
// Changed: the ones its own game holds changes for, so those go back to the host's state too.
struct CellStateRequest final : MessageOf<CellStateRequest, kCellStateRequest>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Cell, Wire::List(Changed, kMaxChanged));
    }
    bool operator==(const CellStateRequest&) const noexcept = default;

    // A cell holds a few thousand references at most; more is a corrupt count.
    static constexpr uint64_t kMaxChanged = 0x4000;
    GameId Cell{};
    std::vector<ChangedReference> Changed;
};
