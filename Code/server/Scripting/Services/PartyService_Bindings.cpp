#include "GameServer.h"

namespace Script
{
void CreatePartyServiceBindings(sol::state_view aState)
{
    auto partyType =
        aState.new_usertype<Server::PartyService>("PartyService", sol::meta_function::construct, sol::no_constructor);

    partyType["get"] = []() -> Server::PartyService& { return Server::GameServer::Get()->GetWorld().GetPartyService(); };
    partyType["IsPlayerInParty"] = [](Server::PartyService& aService, uint32_t aConnID) -> bool {
        Server::Player* player = Server::PlayerManager::Get()->GetByConnectionId(aConnID);
        if (player == nullptr)
            return false;
        return aService.IsPlayerInParty(player);
    };
}
} // namespace Script
