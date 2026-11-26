#include <catch2/catch.hpp>

#include <animation/DeferredClips.h>
#include <core/WorldGate.h>

#include <chrono>
#include <thread>

TEST_CASE("A copy's clip request is made at the main loop, and not for a copy released since", "[animation][deferred-clips]")
{
    int loads = 0;
    bool copy = true;
    const auto cRequest = [&] {
        return DeferClipRequest([&] { return copy; }, [&](const auto&) {
            ++loads;
            return true;
        });
    };

    SECTION("nothing loads where it was asked for; the load starts at the drain and sets the flag itself")
    {
        const auto cDone = cRequest();
        CHECK(loads == 0);
        MainLoopTasks::Run();
        CHECK(loads == 1);
        CHECK_FALSE(cDone->load());
    }
    SECTION("released before the drain: no load on the actor's own process, nothing to wait for")
    {
        const auto cDone = cRequest();
        copy = false;
        MainLoopTasks::Run();
        CHECK(loads == 0);
        CHECK(cDone->load());
    }
    SECTION("no file to load: nothing to wait for")
    {
        const auto cDone = DeferClipRequest([] { return true; }, [](const auto&) { return false; });
        MainLoopTasks::Run();
        CHECK(cDone->load());
    }
}

TEST_CASE("The World's teardown waits for a hook's reader admitted before it", "[world-gate]")
{
    WorldGate::Open();
    std::atomic<bool> admitted{false}, release{false}, closed{false};
    std::thread worker([&] {
        const WorldGate::Reader cReader; // the animation worker, past the check
        admitted = static_cast<bool>(cReader);
        while (!release)
            std::this_thread::yield();
    });
    while (!admitted)
        std::this_thread::yield();
    std::thread unload([&] {
        WorldGate::Close();
        closed = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(closed); // the reader is still inside
    {
        const WorldGate::Reader cLate; // a hook entered once the unload began
        CHECK_FALSE(static_cast<bool>(cLate));
    }
    release = true;
    worker.join();
    unload.join();
    CHECK(closed);
    CHECK_FALSE(static_cast<bool>(WorldGate::Reader{}));
    WorldGate::Open();
    CHECK(static_cast<bool>(WorldGate::Reader{}));
    WorldGate::Close();
}
