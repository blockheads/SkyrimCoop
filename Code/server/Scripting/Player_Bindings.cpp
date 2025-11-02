
#include "Game/PlayerManager.h"
#include "GameServer.h"

namespace Script
{
namespace
{
void BindPlayer(sol::state_view aState)
{
    auto playerType = aState.new_usertype<Server::Player>("Player", sol::constructors<Server::Player(ConnectionId_t)>());

    playerType["GetId"] = &Server::Player::GetId;
    playerType["GetConnectionId"] = &Server::Player::GetConnectionId;
    playerType["GetCharacter"] = &Server::Player::GetCharacter;
    playerType["GetParty"] = &Server::Player::GetParty;
    playerType["GetUsername"] = &Server::Player::GetUsername;
    playerType["GetEndPoint"] = &Server::Player::GetEndPoint;
    playerType["GetDiscordId"] = &Server::Player::GetDiscordId;
    playerType["GetStringCacheId"] = &Server::Player::GetStringCacheId;
    playerType["GetLevel"] = &Server::Player::GetLevel;
    // playerType["GetCellComponent"] = sol::overload(&Server::Player::GetCellComponent, &Server::Player::GetCellComponent);
    //  playerType["GetQuestLogComponent"] = sol::overload(&Server::Player::GetQuestLogComponent,
    //  &Server::Player::GetQuestLogComponent);
    playerType["SetDiscordId"] = &Server::Player::SetDiscordId;
    playerType["SetEndpoint"] = &Server::Player::SetEndpoint;
    playerType["SetUsername"] = &Server::Player::SetUsername;
    playerType["SetMods"] = &Server::Player::SetMods;
    playerType["SetModIds"] = &Server::Player::SetModIds;
    playerType["SetCharacter"] = &Server::Player::SetCharacter;
    playerType["SetStringCacheId"] = &Server::Player::SetStringCacheId;
    playerType["SetLevel"] = &Server::Player::SetLevel;
    playerType["SetCellComponent"] = &Server::Player::SetCellComponent;
    playerType["Send"] = &Server::Player::Send;
    playerType["IsPartyLeader"] = [](Server::Player& aSelf) {
        return Server::GameServer::Get()->GetWorld().GetPartyService().IsPlayerLeader(
            Server::PlayerManager::Get()->GetByConnectionId(aSelf.GetConnectionId()));
    };
}

void BindPlayerManager(sol::state_view aState)
{
    auto table =
        aState.new_usertype<Server::PlayerManager>("PlayerManager", sol::meta_function::construct, sol::no_constructor);
    table["get"] = []() -> Server::PlayerManager& { return Server::GameServer::Get()->GetWorld().GetPlayerManager(); };

    table["GetByConnectionId"] = [](Server::PlayerManager& aSelf, uint32_t aConnID) -> Server::Player const* {
        return aSelf.GetByConnectionId(aConnID);
    };
    table["GetById"] = [](Server::PlayerManager& aSelf, uint32_t aConnID) -> Server::Player const* { return aSelf.GetById(aConnID); };
    table["GetAllPlayers"] = [](Server::PlayerManager& aSelf) {
        std::vector<Server::Player*> allPlayers;
        aSelf.ForEach([&](Server::Player* aPlayer) { allPlayers.push_back(aPlayer); });
        return allPlayers;
    };

    table["Count"] = &Server::PlayerManager::Count;
}

} // namespace

void CreatePlayerBindings(sol::state_view aState)
{
    BindPlayer(aState);
    BindPlayerManager(aState);
}
} // namespace Script
