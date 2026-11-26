#pragma once

#include <cstdint>
#include <functional>

namespace Payloads
{
struct TrapContact;
}

// What the host's rules read and change in its game: a guest's combat made in the host's game. A part of HostGame.
struct HostCombatGame
{
    // A trap touched a guest's player in its game: raised here for its copy, the trap's script decides.
    std::function<void(const Payloads::TrapContact& acContact)> TouchTrap;
};
