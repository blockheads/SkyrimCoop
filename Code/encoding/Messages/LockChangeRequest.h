#pragma once

#include "Message.h"

#include <Structs/GameId.h>

struct LockChangeRequest final : MessageOf<LockChangeRequest, kLockChangeRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id, CellId, IsLocked, Wire::Bits<8>(LockLevel)); }
    bool operator==(const LockChangeRequest&) const noexcept = default;

    GameId Id;
    GameId CellId;
    bool IsLocked{};
    uint8_t LockLevel{};
};
