
#include "GameServer.h"

#include <Messages/NotifyChatMessageBroadcast.h>
#include <regex>

namespace Script
{
void CreateGameServerBindings(sol::state_view aState)
{
    auto type = aState.new_usertype<Server::GameServer>("GameServer", sol::meta_function::construct, sol::no_constructor);
    type["get"] = []() { return Server::GameServer::Get(); };
    type["Kill"] = &Server::GameServer::Kill;
    type["Kick"] = &Server::GameServer::Kick;
    type["GetTick"] = &Server::GameServer::GetTick;

    type["SendChatMessage"] = [](Server::GameServer& aSelf, ConnectionId_t aConnectionId, const std::string& acMessage) {
        NotifyChatMessageBroadcast notifyMessage{};

        std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"};
        notifyMessage.MessageType = ChatMessageType::kLocalChat;
        notifyMessage.PlayerName = "[Server]";
        notifyMessage.ChatMessage = std::regex_replace(acMessage, escapeHtml, "");
        Server::GameServer::Get()->Send(aConnectionId, notifyMessage);
    };
    type["SendGlobalChatMessage"] = [](Server::GameServer& aSelf, const std::string& acMessage) {
        NotifyChatMessageBroadcast notifyMessage{};

        std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"};
        notifyMessage.MessageType = ChatMessageType::kGlobalChat;
        notifyMessage.PlayerName = "[Server]";
        notifyMessage.ChatMessage = std::regex_replace(acMessage, escapeHtml, "");
        Server::GameServer::Get()->SendToPlayers(notifyMessage);
    };
    type["SetTime"] = [](Server::GameServer& aSelf, int aHours, int aMinutes,
                         float aScale = Server::GameServer::Get()->GetWorld().GetCalendarService().GetTimeScale()) -> bool {
        aHours = std::max(0, std::min(aHours, 23));
        aMinutes = std::max(0, std::min(aMinutes, 59));
        aScale = std::max(1.0f, std::min(aScale, 100.0f));

        return Server::GameServer::Get()->GetWorld().GetCalendarService().SetTime(aHours, aMinutes, aScale);
    };
    // type["SendPacket"]
}
} // namespace Script
