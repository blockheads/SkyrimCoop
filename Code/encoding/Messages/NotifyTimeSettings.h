#pragma once

#include "Message.h"
#include "Structs/TimeModel.h"

struct NotifyTimeSettings final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyTimeSettings;

    NotifyTimeSettings()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyTimeSettings& achRhs) const noexcept
    {
        return GetOpcode() == achRhs.GetOpcode() && timeModel == achRhs.timeModel;
    }

    bool operator!=(const NotifyTimeSettings& achRhs) const noexcept
    {
        return !this->operator==(achRhs);
    }

    TimeModel timeModel;
};
