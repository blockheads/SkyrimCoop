
#include "GameServer.h"


namespace Script
{
void CreateGameServerBindings(sol::state_view aState)
{
    auto type = aState.new_usertype<Server::GameServer>("GameServer", sol::meta_function::construct, sol::no_constructor);
    type["get"] = []() { return Server::GameServer::Get(); };
    type["Kill"] = &Server::GameServer::Kill;
    type["Kick"] = &Server::GameServer::Kick;
    type["GetTick"] = &Server::GameServer::GetTick;

    // type["SendPacket"]
}
} // namespace Script
