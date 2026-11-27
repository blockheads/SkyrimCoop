#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct NotifyPlayerLeft final : MessageOf<NotifyPlayerLeft, kNotifyPlayerLeft>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(PlayerId, Username); }
    bool operator==(const NotifyPlayerLeft&) const noexcept = default;

    uint32_t PlayerId{};
    std::string Username;
};
