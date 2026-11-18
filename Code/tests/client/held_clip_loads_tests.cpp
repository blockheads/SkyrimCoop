#include <catch2/catch.hpp>

#include <Games/Animation/HeldClipLoads.h>

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
    bool Released = false;
};

// The manager's side: loads queued with the lock they name, cancels, releases.
struct Loader
{
    HeldClipLoads<Set, TestLock> Held;
    std::vector<const TestLock*> Locks; // each queued load's lock
    std::vector<void*> Cancelled;
    std::vector<std::string> Released;
    std::set<uint32_t> Gone;
    uintptr_t Next = 0;

    void* Request(uint32_t aFormId, bool aCallsBack = false)
    {
        return Held.Request(
            aFormId, [this](uint32_t aId) { return Gone.contains(aId); },
            [&](auto& aHolder) {
                Locks.push_back(&aHolder.LoadLock);
                if (aCallsBack)
                    Held.Loaded(aFormId, [](Set& aSet) { aSet.Event = "at once"; });
                return reinterpret_cast<void*>(++Next);
            },
            [this](void* apLoad) { Cancelled.push_back(apLoad); },
            [this](Set& aSet) {
                aSet.Released = true;
                Released.push_back(aSet.Event);
            });
    }
};
} // namespace

TEST_CASE("A held clip load names its holder's lock, and its callback ends it", "[held-clip-loads]")
{
    Loader loader;
    void* pLoad = loader.Request(0xFF000821);
    REQUIRE(pLoad);
    REQUIRE(loader.Locks.size() == 1);
    CHECK(loader.Locks[0]->Depth == 0); // released once queued
    CHECK(loader.Held.Loaded(0xFF000821, [](Set& aSet) { aSet.Event = "TurnToWalk"; }));
    // A second load for it cancels nothing: the first one ended.
    loader.Request(0xFF000821);
    CHECK(loader.Cancelled.empty());
    CHECK(loader.Locks[1] == loader.Locks[0]); // the same holder's
}

TEST_CASE("A held clip load under way is cancelled when a new one replaces it", "[held-clip-loads]")
{
    Loader loader;
    void* pFirst = loader.Request(0xFF000821);
    loader.Request(0xFF000821);
    CHECK(loader.Cancelled == std::vector<void*>{pFirst});
}

TEST_CASE("A gone copy's held clip load is cancelled before its holder is released", "[held-clip-loads]")
{
    Loader loader;
    void* pGone = loader.Request(0xFF000821);
    loader.Gone.insert(0xFF000821);
    loader.Request(0xFF000822); // the next request sweeps the gone ones
    CHECK(loader.Cancelled == std::vector<void*>{pGone});
    CHECK(loader.Released.size() == 1);
    CHECK(loader.Held.Size() == 1);
    CHECK_FALSE(loader.Held.Loaded(0xFF000821, [](Set&) {})); // a late callback finds no holder
}

TEST_CASE("A held clip load that called back at once is not kept to cancel", "[held-clip-loads]")
{
    Loader loader;
    loader.Request(0xFF000821, true);
    loader.Gone.insert(0xFF000821);
    loader.Request(0xFF000822);
    CHECK(loader.Cancelled.empty());
    CHECK(loader.Released == std::vector<std::string>{"at once"});
}
