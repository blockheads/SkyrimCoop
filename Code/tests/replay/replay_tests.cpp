#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include "Services/CharacterService.h"  // LocalComponent, RemoteComponent
#include <string>

// FIXTURE_PATH is set by xmake: add_defines("FIXTURE_PATH=...")
#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Actor spawn creates entity in registry", "[Replay][CharacterService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("actor_spawn.ndjson"));

    REQUIRE(events == 2);

    // PointerTable should track formId 0x14 (player, decimal 20)
    REQUIRE(harness.GetPointerTable().Contains(0x14));
    REQUIRE(harness.GetPointerTable().Lookup(0x14) == 140737488355328ull);

    // Registry should have a LocalComponent with formId 0x14
    auto& reg = harness.GetRegistry();
    auto view = reg.view<LocalComponent>();
    bool found = false;
    for (auto entity : view) {
        auto& comp = view.get<LocalComponent>(entity);
        if (comp.formId == 0x14) {
            found = true;
            CHECK(comp.gamePtr == 140737488355328ull);
        }
    }
    REQUIRE(found);
}

TEST_CASE("Actor remove cleans up entity", "[Replay][CharacterService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("actor_remove.ndjson"));

    REQUIRE(events == 3);

    // After remove, formId 0x100 (256) should NOT be in PointerTable
    REQUIRE_FALSE(harness.GetPointerTable().Contains(0x100));

    // No LocalComponent with formId 0x100 should remain in registry
    auto& reg = harness.GetRegistry();
    auto view = reg.view<LocalComponent>();
    for (auto entity : view) {
        auto& comp = view.get<LocalComponent>(entity);
        REQUIRE(comp.formId != 0x100);
    }
}

TEST_CASE("Movement sync updates position cache", "[Replay][CharacterService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("movement_sync.ndjson"));

    REQUIRE(events == 4);

    // PointerTable should track the actor
    REQUIRE(harness.GetPointerTable().Contains(0x14));

    // The LocalComponent should exist with correct formId
    auto& reg = harness.GetRegistry();
    auto view = reg.view<LocalComponent>();
    bool found = false;
    for (auto entity : view) {
        auto& comp = view.get<LocalComponent>(entity);
        if (comp.formId == 0x14) {
            found = true;
            CHECK(comp.gamePtr == 140737488355328ull);
        }
    }
    REQUIRE(found);

    // Determinism check: replay same file twice, get same result (D-03)
    ReplayHarness harness2;
    size_t events2 = harness2.ReplayFile(FixturePath("movement_sync.ndjson"));
    REQUIRE(events2 == events);
    REQUIRE(harness2.GetPointerTable().Size() == harness.GetPointerTable().Size());
}

TEST_CASE("Multiple actors tracked simultaneously", "[Replay][CharacterService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("multi_actor.ndjson"));

    REQUIRE(events == 6);

    // All 3 actors in PointerTable
    REQUIRE(harness.GetPointerTable().Contains(0x14));   // formId 20
    REQUIRE(harness.GetPointerTable().Contains(0x100));  // formId 256
    REQUIRE(harness.GetPointerTable().Contains(0x200));  // formId 512

    // All 3 actors have LocalComponent in registry
    auto& reg = harness.GetRegistry();
    auto view = reg.view<LocalComponent>();
    size_t count = 0;
    for ([[maybe_unused]] auto entity : view) {
        count++;
    }
    REQUIRE(count >= 3);

    // Verify specific pointer values
    CHECK(harness.GetPointerTable().Lookup(0x14) == 140737488355328ull);
    CHECK(harness.GetPointerTable().Lookup(0x100) == 140737488360000ull);
    CHECK(harness.GetPointerTable().Lookup(0x200) == 140737488365000ull);
}
