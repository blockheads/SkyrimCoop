#pragma once

#include "Message.h"

#include <Structs/ActionEvent.h>
#include <Structs/ActorData.h>
#include <Wire/Buffer.h>

struct RequestRespawn final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kRequestRespawn;

    RequestRespawn()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestRespawn& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ActorId == acRhs.ActorId && AppearanceBuffer == acRhs.AppearanceBuffer &&
               ChangeFlags == acRhs.ChangeFlags && HasState == acRhs.HasState && LatestAction == acRhs.LatestAction &&
               GraphState == acRhs.GraphState && Data == acRhs.Data;
    }

    uint32_t ActorId{};
    std::string AppearanceBuffer{};
    uint32_t ChangeFlags{0};
    // The owner's state now, sent with its own respawn: what the new copies start from.
    bool HasState{false};
    ActionEvent LatestAction{};
    std::string GraphState{};
    ActorData Data{};
};
