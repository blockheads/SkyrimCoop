#include <catch2/catch.hpp>

#include <Games/ItemMenuGate.h>

#include <atomic>
#include <thread>

namespace
{
// A clock that advances a step each time it is read; a pause that runs the other side's step once.
struct Steps
{
    uint64_t Micros{};
    uint64_t Step = 100;
    uint64_t operator()() noexcept { return Micros += Step; }
};
} // namespace

TEST_CASE("The item menu gate keeps a listed removal and the UI's read apart", "[inventory][item-menu][item-menu-gate]")
{
    ItemMenuGate gate;
    Steps clock;

    SECTION("nothing in flight: the UI reads at once, a removal after it waits until it ends")
    {
        CHECK(gate.UiBegin(clock, [] {}));
        int pauses = 0;
        CHECK(gate.RemovalBegin(clock, [&] {
            if (++pauses == 3)
                gate.UiEnd(); // the UI's call returns while the removal waits
        }));
        CHECK(pauses == 3);
        CHECK(gate.InFlight() == 1);
        gate.RemovalEnd();
        CHECK(gate.InFlight() == 0);
    }
    SECTION("a removal in flight: the UI waits until it ends")
    {
        CHECK(gate.RemovalBegin(clock, [] {}));
        int pauses = 0;
        CHECK(gate.UiBegin(clock, [&] {
            if (++pauses == 2)
                gate.RemovalEnd();
        }));
        CHECK(pauses == 2);
        gate.UiEnd();
    }
    SECTION("both at once: the removal steps back, so the UI goes first and nobody waits for the other forever")
    {
        // The removal counted itself, then saw the UI reading: it uncounts and waits.
        std::atomic<bool> uiDone{};
        CHECK(gate.UiBegin(clock, [] {}));
        CHECK(gate.InFlight() == 0);
        int pauses = 0;
        CHECK(gate.RemovalBegin(clock, [&] {
            CHECK(gate.InFlight() == 0); // stepped back while waiting: the UI is not held by it
            if (++pauses == 2)
            {
                gate.UiEnd();
                uiDone = true;
            }
        }));
        CHECK(uiDone);
        gate.RemovalEnd();
    }
    SECTION("a UI call that does not end: the removal goes on after the wait, counted")
    {
        CHECK(gate.UiBegin(clock, [] {}));
        CHECK_FALSE(gate.RemovalBegin(clock, [] {}));
        CHECK(clock.Micros >= ItemMenuGate::kWaitMicros);
        CHECK(gate.InFlight() == 1);
        gate.RemovalEnd();
        gate.UiEnd();
    }
    SECTION("a removal that does not end: the UI goes on after the wait, counted")
    {
        CHECK(gate.RemovalBegin(clock, [] {}));
        CHECK_FALSE(gate.UiBegin(clock, [] {}));
        CHECK(gate.Reading());
        gate.UiEnd();
        gate.RemovalEnd();
    }
}

TEST_CASE("Under two threads the gate never lets a removal run inside the UI's read", "[inventory][item-menu][item-menu-gate]")
{
    ItemMenuGate gate;
    std::atomic<int> inside{};      // 1: the UI reads, 2: a removal runs
    std::atomic<int> overlaps{};
    std::atomic<int> timeouts{};
    const auto cWallClock = [] {
        const auto cStart = std::chrono::steady_clock::now();
        return [cStart] { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - cStart).count()); };
    };
    const auto cPause = [] { std::this_thread::yield(); };
    const auto cEnter = [&](int aWho) {
        if (inside.exchange(aWho) != 0)
            ++overlaps;
        inside = 0;
    };
    std::thread ui([&] {
        for (int i = 0; i < 20000; ++i)
        {
            if (!gate.UiBegin(cWallClock(), cPause))
                ++timeouts;
            cEnter(1);
            gate.UiEnd();
        }
    });
    std::thread remover([&] {
        for (int i = 0; i < 20000; ++i)
        {
            if (!gate.RemovalBegin(cWallClock(), cPause))
                ++timeouts;
            cEnter(2);
            gate.RemovalEnd();
        }
    });
    ui.join();
    remover.join();
    CHECK(timeouts == 0);
    CHECK(overlaps == 0);
    CHECK(gate.InFlight() == 0);
    CHECK_FALSE(gate.Reading());
}
