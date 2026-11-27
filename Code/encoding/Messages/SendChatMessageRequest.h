#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct SendChatMessageRequest final : MessageOf<SendChatMessageRequest, kSendChatMessageRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(ChatMessage); }
    bool operator==(const SendChatMessageRequest&) const noexcept = default;

    std::string ChatMessage;
};
