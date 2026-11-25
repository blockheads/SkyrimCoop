#include <Services/Host/HostChat.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostRouter.h>

#include <ChatMessageTypes.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/SendChatMessageRequest.h>

#include <algorithm>

HostChat::HostChat(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
{
    aRouter.On(*this, &HostChat::OnChatMessage);
}

void HostChat::OnChatMessage(const SendChatMessageRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    // The chat box shows text as it is (RmlUi text, no markup parsed).
    NotifyChatMessageBroadcast notify;
    notify.MessageType = kPlayerChat;
    notify.PlayerName = ChatText(m_players.PlayerNameOf(aSender));
    notify.ChatMessage = ChatText(acRequest.ChatMessage);
    for (const ConnectionId_t cPlayer : m_players.Connections())
        m_network.Send(cPlayer, notify);
}

std::string ChatText(std::string_view aText) noexcept
{
    // A character the limit cuts through is left out whole (UTF-8 continuation bytes are 10xxxxxx).
    size_t length = std::min(aText.size(), kChatMaxLength);
    while (length && length < aText.size() && (static_cast<unsigned char>(aText[length]) & 0xC0) == 0x80)
        --length;
    std::string text;
    for (const char c : aText.substr(0, length))
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
            text += c;
    return text;
}
