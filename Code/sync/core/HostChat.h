#pragma once

#include <NoCopyMove.h>
#include <network/ENetInterface.h>

#include <string>
#include <string_view>

struct HostNetwork;
struct HostPlayers;
struct HostRouter;
struct SendChatMessageRequest;

// The party's chat: a line goes to everyone, the sender too (its chat shows what it sent), under its name.
struct HostChat
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rule with aRouter.
    HostChat(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers) noexcept;
    NO_COPY_MOVE(HostChat);

  private:
    void OnChatMessage(const SendChatMessageRequest& acRequest, ConnectionId_t aSender) const noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
};

// A player's text as chat shows it: at most kChatMaxLength bytes (whole characters), no control characters.
[[nodiscard]] std::string ChatText(std::string_view aText) noexcept;
