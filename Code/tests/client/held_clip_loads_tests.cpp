#include <catch2/catch.hpp>

#include <animation/HeldClipLoads.h>

#include <set>
#include <string>
#include <vector>

namespace
{
struct TestLock
{
    void lock() noexcept { ++Depth; }
    void unlock() noexcept { --Depth; }
    int Depth = 0;
};

struct Set
{
    std::string Event;
};

// The manager's side: each load queued with the lock and callback it names, the callbacks and holders released.
struct Loader
{
    HeldClipLoads<Set, TestLock> Held;
    std::vector<const TestLock*> Locks;
    std::vector<uint32_t> Numbers; // each queued load's
    std::vector<void*> ReleasedCallbacks;
    std::vector<std::string> Released;
    std::set<uint32_t> Gone;

    void* Request(uint32_t aFormId, bool aCallsBack = false)
    {
        return Held.Request(
            aFormId, [this](uint32_t aId) { return Gone.contains(aId); },
            [](uint32_t aNumber) { return reinterpret_cast<void*>(uintptr_t(aNumber)); },
            [&](auto& aHolder, void* apCallback) {
                const auto cNumber = uint32_t(reinterpret_cast<uintptr_t>(apCallback));
                Locks.push_back(&aHolder.LoadLock);
                Numbers.push_back(cNumber);
                if (aCallsBack)
                    Held.Loaded(aFormId, cNumber, [](Set& aSet) { aSet.Event = "at once"; });
                return apCallback;
            },
            [this](void* apCallback) { ReleasedCallbacks.push_back(apCallback); },
            [this](Set& aSet) { Released.push_back(aSet.Event); });
    }
};
} // namespace

TEST_CASE("A held clip load names its holder's lock, and its callback ends it", "[held-clip-loads]")
{
    Loader loader;
    REQUIRE(loader.Request(0xFF000821));
    REQUIRE(loader.Locks.size() == 1);
    CHECK(loader.Locks[0]->Depth == 0); // released once queued
    CHECK(loader.Held.Loaded(0xFF000821, loader.Numbers[0], [](Set& aSet) { aSet.Event = "TurnToWalk"; }));
    CHECK(loader.ReleasedCallbacks.empty()); // not in the engine's own call
    loader.Request(0xFF000821);
    CHECK(loader.ReleasedCallbacks.size() == 1); // at the next request
    CHECK(loader.Locks[1] == loader.Locks[0]);   // the same holder's
}

TEST_CASE("A newer held clip load leaves the older one queued and keeps only its own set", "[held-clip-loads]")
{
    Loader loader;
    loader.Request(0xFF000821);
    loader.Request(0xFF000821);
    // The older load's set arrives after: not kept (nothing cancels a load: 63997 takes its lock first).
    CHECK(loader.Held.Loaded(0xFF000821, loader.Numbers[1], [](Set& aSet) { aSet.Event = "newer"; }));
    CHECK_FALSE(loader.Held.Loaded(0xFF000821, loader.Numbers[0], [](Set& aSet) { aSet.Event = "older"; }));
    loader.Gone.insert(0xFF000821);
    loader.Request(0xFF000822);
    CHECK(loader.Released == std::vector<std::string>{"newer"});
}

TEST_CASE("A gone copy's holder stays while a load of its is queued, and goes after it", "[held-clip-loads]")
{
    Loader loader;
    loader.Request(0xFF000821);
    const auto* pLock = loader.Locks[0];
    loader.Gone.insert(0xFF000821);
    loader.Request(0xFF000822);
    CHECK(loader.Released.empty()); // its load still names its lock
    CHECK(loader.Held.Size() == 2);
    CHECK(pLock->Depth == 0);
    // The load ends; the next request releases the holder and its callback.
    CHECK(loader.Held.Loaded(0xFF000821, loader.Numbers[0], [](Set& aSet) { aSet.Event = "late"; }));
    loader.Request(0xFF000822);
    CHECK(loader.Released == std::vector<std::string>{"late"});
    CHECK(loader.Held.Size() == 1);
    CHECK_FALSE(loader.Held.Loaded(0xFF000821, loader.Numbers[0], [](Set&) {}));
}

TEST_CASE("A held clip load that called back at once leaves nothing queued", "[held-clip-loads]")
{
    Loader loader;
    loader.Request(0xFF000821, true);
    loader.Gone.insert(0xFF000821);
    loader.Request(0xFF000822);
    CHECK(loader.Released == std::vector<std::string>{"at once"});
    CHECK(loader.Held.Size() == 1);
}
