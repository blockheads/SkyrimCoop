#include "GameServer.h"

namespace Script
{
void CreateCharacterServiceBindings(sol::state_view aState)
{
    auto characterType =
        aState.new_usertype<Server::CharacterService>("CharacterService", sol::meta_function::construct, sol::no_constructor);

    characterType["get"] = []() -> Server::CharacterService& { return Server::GameServer::Get()->GetWorld().GetCharacterService(); };
}
} // namespace Script
