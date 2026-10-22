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

// A reference whose change record holds these flags (TESObjectREFR::ChangeFlags) in the sender's game.
struct ChangedReference
{
    GameId Id{};
    uint32_t Flags{};

    bool operator==(const ChangedReference& acRhs) const noexcept { return Id == acRhs.Id && Flags == acRhs.Flags; }
};

// A guest's cell finished loading: it asks for the host's state of the references there (world-state sync, step 2).
// Changed: the ones its own game holds changes for, so those go back to the host's state too.
struct CellStateRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kCellStateRequest;

    CellStateRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const CellStateRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Cell == acRhs.Cell && Changed == acRhs.Changed;
    }

    GameId Cell{};
    std::vector<ChangedReference> Changed;
};
