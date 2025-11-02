#include "GameServer.h"
#include "World.h"

namespace Script
{
void CreateWorldBindings(sol::state_view aState)
{
    auto type =
        aState.new_usertype<Server::World>("World", sol::meta_function::construct, sol::no_constructor);

    type["get"] = []() -> Server::World& { return Server::GameServer::Get()->GetWorld(); };
}
} // namespace Script
