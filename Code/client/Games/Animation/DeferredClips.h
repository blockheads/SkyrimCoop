#pragma once

#include <MainLoopTasks.h>

#include <atomic>
#include <memory>
#include <utility>

// A copy's clip request comes from its graph's update, its graph manager's lock held (63358). The loader's job takes
// that lock inside a callback, under the locks a request takes (63997), so the request is made between frames
// (docs/re/systems/animation-file-loads.md, "A request under a graph's lock"). aLoad(done) starts the load that sets
// done; false: nothing to wait for. Returns done.
template <class Load> [[nodiscard]] std::shared_ptr<std::atomic<bool>> DeferClipRequest(Load aLoad)
{
    auto done = std::make_shared<std::atomic<bool>>(false);
    MainLoopTasks::Queue([done, load = std::move(aLoad)]() mutable {
        if (!load(done))
            done->store(true, std::memory_order_release);
    });
    return done;
}
