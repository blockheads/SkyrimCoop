#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct ActivateRequest final : MessageOf<ActivateRequest, kActivateRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id, CellId, ActivatorId, NpcId); }
    bool operator==(const ActivateRequest&) const noexcept = default;

    GameId Id;
    uint32_t NpcId{};
    GameId CellId;
    uint32_t ActivatorId{};
};
