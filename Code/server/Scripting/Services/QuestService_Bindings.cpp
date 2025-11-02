#include "GameServer.h"

namespace Script
{
void CreateQuestServiceBindings(sol::state_view aState)
{
    auto questType =
        aState.new_usertype<Server::QuestService>("QuestService", sol::meta_function::construct, sol::no_constructor);

    questType["get"] = []() -> Server::QuestService& { return Server::GameServer::Get()->GetWorld().GetQuestService(); };
}
} // namespace Script
