#pragma once

struct BSAnimationGraphManager;

// An actor's behaviour graph state as a savegame stores it, through the engine's own serializer (63601/63602)
// into memory. A copy loads its owner's at spawn (docs/re/systems/actor-ai-and-copies.md).
namespace AnimationGraphState
{
// Each graph of the manager, by behaviour project: a player has its third and first person graphs.
bool Save(BSAnimationGraphManager* apManager, std::string& aState) noexcept;
// Loads each saved graph into the manager's graph of the same project; false if none matched or one failed.
bool Load(BSAnimationGraphManager* apManager, const std::string& acState) noexcept;
} // namespace AnimationGraphState
