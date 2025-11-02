
#include "GameServer.h"

namespace Script
{
namespace
{
void BindMovementComponent(sol::state_view aState)
{
    aState["GetMovementComponent"] = [](entt::entity aEntity) {
        return Server::GameServer::Get()->GetWorld().try_get<Server::MovementComponent>(aEntity);
    };

    auto table =
        aState.new_usertype<Server::MovementComponent>("MovementComponent", sol::constructors<Server::MovementComponent()>());
    table["Tick"] = &Server::MovementComponent::Tick;
    table["Position"] = &Server::MovementComponent::Position;
    table["Rotation"] = &Server::MovementComponent::Rotation;
    // movementComponentType["Variables"] = &Server::MovementComponent::Variables;
    table["Direction"] = &Server::MovementComponent::Direction;
    table["Sent"] = &Server::MovementComponent::Sent;
}
} // namespace

void CreateComponentBindings(sol::state_view aState)
{
    BindMovementComponent(aState);
}
} // namespace Script
