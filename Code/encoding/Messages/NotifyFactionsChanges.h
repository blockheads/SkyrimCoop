#pragma once

#include "Message.h"
#include <Structs/Factions.h>

struct NotifyFactionsChanges final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyFactionsChanges;

    NotifyFactionsChanges()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyFactionsChanges& acRhs) const noexcept { return Changes == acRhs.Changes && GetOpcode() == acRhs.GetOpcode(); }

    std::map<uint32_t, Factions> Changes{};
};
