#include "GameServer.h"

namespace Script
{
void CreateScriptServiceBindings(sol::state_view aState)
{
    auto scriptType =
        aState.new_usertype<Server::ScriptService>("ScriptService", sol::meta_function::construct, sol::no_constructor);

    scriptType["get"] = []() -> Server::ScriptService& { return Server::GameServer::Get()->GetWorld().GetScriptService(); };
}
} // namespace Script
