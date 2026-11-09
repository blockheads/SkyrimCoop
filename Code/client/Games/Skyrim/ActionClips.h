#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

struct Actor;

namespace ActionClips
{
// A process's clip sets a copy mirrors (40055 keeps a set under its kind).
enum Set : uint8_t
{
    kAction = 3,  // AIProcess 39625
    kInstant = 4, // an instant interaction's (39291): furniture and idle markers entered at once
    kMovement = 47, // a movement idle's (IMovementSelectIdle 41694): no process slot, a copy holds it here
};

// Loads an event's clip files into one of the actor's process's sets, as its owner's game did (coop_LoadEventClips
// 43621). Null: nothing to wait for. Otherwise set once they are in.
std::shared_ptr<std::atomic<bool>> Request(Actor& aActor, const char* acEvent, Set aSet = kAction) noexcept;
} // namespace ActionClips
