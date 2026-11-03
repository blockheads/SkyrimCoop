#pragma once

#include <cstddef>
#include <cstdint>

// The engine's main object (CommonLibSSE RE::Main, singleton 403449). freezeTime skips the world's update in
// Main::Update (36564): calendar, sky, the player, ProcessLists, projectiles, TES; the VM update (ours) still runs.
struct Main
{
    static Main* Get() noexcept;

    uint8_t pad0[0x16];
    bool freezeTime;      // 0x16
    bool freezeNextFrame; // 0x17
};
static_assert(offsetof(Main, freezeTime) == 0x16);
static_assert(offsetof(Main, freezeNextFrame) == 0x17);
