#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct SendChatMessageRequest final : MessageOf<SendChatMessageRequest, kSendChatMessageRequest>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(ChatMessage); }
    bool operator==(const SendChatMessageRequest&) const noexcept = default;

    std::string ChatMessage;
};
