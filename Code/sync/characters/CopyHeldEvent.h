#pragma once

#include <cstdint>

#include <entt/entity/entity.hpp>

// One of this game's references lost its owner's actor and stays a held copy (CharacterService::FollowOwner,
// DropCopies), before it is moved where the owner's went.
struct CopyHeldEvent
{
    entt::entity Entity;
    uint32_t FormId;
    bool OwnerDisabled = false; // its owner's actor was disabled or deleted: it stays disabled after the session
};
