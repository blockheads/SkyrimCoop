#pragma once

#include <Wire/Archive.h>

#include <Wire/Buffer.h>

#include <cstdint>

// An actor's state beside its graph, as its owner last had it (one-simulator.md, piece 2): its masked flags, its kill
// move, drawn hands and noise. Applied on a copy when its stream reaches Tick.
struct ActorStatePart
{
    bool operator==(const ActorStatePart&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Tick, Flags1, Flags2, InKillMove, Killer, HandsUp, Noise); }
    // The state alone, its tick aside: whether an owner has anything new to declare.
    [[nodiscard]] bool SameState(const ActorStatePart& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    uint64_t Tick{};   // the owner's frame, on the session's clock
    uint32_t Flags1{}; // ActorState flags1 under ActorStateRules::kPartBits1
    uint32_t Flags2{}; // flags2 under kPartBits2
    bool InKillMove{}; // Actor::IsInKillMove (38615)
    uint32_t Killer{}; // its killer's session id + 1 (the victim's half), 0 none
    bool HandsUp{};    // its weapon drawn, settled (weapon state 3), or sheathed (0)
    uint32_t Noise{};  // the noise its actions make now (actionValue, 37711), what detection hears
};
