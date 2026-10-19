#pragma once

#include <cstddef>

// The longest chat line, in bytes: the chat box takes no more, the host cuts what is longer.
constexpr size_t kChatMaxLength = 200;

enum ChatMessageType : unsigned char
{
    kSystemMessage = 0,
    kGlobalChat,
    kPlayerDialogue,
    kPartyChat,
    kLocalChat
};
