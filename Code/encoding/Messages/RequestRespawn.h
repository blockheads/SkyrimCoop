#pragma once

#include "Message.h"

#include <Structs/ActionEvent.h>
#include <Structs/ActorData.h>

struct RequestRespawn final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestRespawn;

    RequestRespawn()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestRespawn& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ActorId == acRhs.ActorId && AppearanceBuffer == acRhs.AppearanceBuffer &&
               ChangeFlags == acRhs.ChangeFlags && HasState == acRhs.HasState && LatestAction == acRhs.LatestAction &&
               GraphState == acRhs.GraphState && Data == acRhs.Data;
    }

    uint32_t ActorId{};
    String AppearanceBuffer{};
    uint32_t ChangeFlags{0};
    // The owner's state now, sent with its own respawn: what the new copies start from.
    bool HasState{false};
    ActionEvent LatestAction{};
    String GraphState{};
    ActorData Data{};
};
