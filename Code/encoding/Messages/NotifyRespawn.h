#pragma once

#include "Message.h"
#include <Wire/Buffer.h>
#include <Structs/Tints.h>

struct NotifyRespawn final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyRespawn;
    NotifyRespawn()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRespawn& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ActorId == acRhs.ActorId && AppearanceBuffer == acRhs.AppearanceBuffer &&
               ChangeFlags == acRhs.ChangeFlags && FaceTints == acRhs.FaceTints && Weight == acRhs.Weight;
    }

    uint32_t ActorId{};
    // With looks, update the existing copy; otherwise make it again from the owner's live state.
    std::string AppearanceBuffer;
    uint32_t ChangeFlags{};
    Tints FaceTints;
    float Weight{50.f};
};
