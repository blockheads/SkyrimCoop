#pragma once

#include "Message.h"
#include "Structs/TimeModel.h"

struct NotifyTimeSettings final : MessageOf<NotifyTimeSettings, kNotifyTimeSettings>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(timeModel); }
    bool operator==(const NotifyTimeSettings&) const noexcept = default;


    TimeModel timeModel;
};
