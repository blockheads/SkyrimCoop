#pragma once

#include "Message.h"
#include <Structs/ServerSettings.h>

struct NotifySettingsChange final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifySettingsChange;

    NotifySettingsChange()
        : NotifyMessage(Opcode)
    {
    }

    virtual ~NotifySettingsChange() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifySettingsChange& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Settings == acRhs.Settings; }

    ServerSettings Settings{};
};
