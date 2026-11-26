#pragma once

#include <core/MainLoopTasks.h>

#include <atomic>
#include <memory>
#include <utility>

// A copy's clip request, made between frames: its graph's update holds the lock 63997's callbacks take
// (animation-file-loads.md). aAlive: still that copy at the drain; aLoad(done) starts the load, false for none.
template <class Alive, class Load> [[nodiscard]] std::shared_ptr<std::atomic<bool>> DeferClipRequest(Alive aAlive, Load aLoad)
{
    auto done = std::make_shared<std::atomic<bool>>(false);
    MainLoopTasks::Queue([done, alive = std::move(aAlive), load = std::move(aLoad)]() mutable {
        if (!alive() || !load(done))
            done->store(true, std::memory_order_release);
    });
    return done;
}
