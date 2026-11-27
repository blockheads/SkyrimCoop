#pragma once

#include "Message.h"

struct NotifySettingsChange final : MessageOf<NotifySettingsChange, kNotifySettingsChange>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Difficulty); }
    bool operator==(const NotifySettingsChange&) const noexcept = default;

    uint32_t Difficulty{};
};
