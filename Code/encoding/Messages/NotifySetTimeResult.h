#pragma once

#include "Message.h"

struct NotifySetTimeResult final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifySetTimeResult;

    NotifySetTimeResult()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifySetTimeResult& achRhs) const noexcept { return GetOpcode() == achRhs.GetOpcode() && Result == achRhs.Result; }

    enum class SetTimeResult : uint8_t
    {
        kSuccess,
        kNoPermission
    };

    SetTimeResult Result{};
};
