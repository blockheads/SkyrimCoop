#pragma once

#include <atomic>
#include <memory>

struct Actor;

namespace ActionClips
{
// Loads an event's clip files into the actor's process, as an owner's game does before its action
// (AIProcess 39625). Null: nothing to wait for. Otherwise set once they are in.
std::shared_ptr<std::atomic<bool>> Request(Actor& aActor, const char* acEvent) noexcept;
} // namespace ActionClips
