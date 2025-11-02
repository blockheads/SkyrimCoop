#include "GameServer.h"

namespace Script
{
void CreatePlayerServiceBindings(sol::state_view aState)
{
    auto playerType =
        aState.new_usertype<Server::PlayerService>("PlayerService", sol::meta_function::construct, sol::no_constructor);

    playerType["get"] = []() -> Server::PlayerService& { return Server::GameServer::Get()->GetWorld().GetPlayerService(); };
}
} // namespace Script
