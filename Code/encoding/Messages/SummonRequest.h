#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// A guest's player cast a summon on itself: the host's game puts that effect on the guest's copy, so the host's engine
// makes the summon (docs/re/systems/summons.md).
struct SummonRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kSummonRequest;

    SummonRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const SummonRequest& acRhs) const noexcept
    {
        return SpellId == acRhs.SpellId && EffectId == acRhs.EffectId && Magnitude == acRhs.Magnitude && GetOpcode() == acRhs.GetOpcode();
    }

    GameId SpellId;
    GameId EffectId; // its summon effect (MGEF)
    float Magnitude{};
};
