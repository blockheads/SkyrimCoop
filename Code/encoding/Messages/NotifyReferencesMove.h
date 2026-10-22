#pragma once

#include "Message.h"
#include <Structs/ReferenceUpdate.h>

struct NotifyReferencesMove final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyReferencesMove;

    NotifyReferencesMove()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyReferencesMove& acRhs) const noexcept { return Updates == acRhs.Updates && Tick == acRhs.Tick && GetOpcode() == acRhs.GetOpcode(); }

    uint64_t Tick{};
    std::map<uint32_t, ReferenceUpdate> Updates{};
};
