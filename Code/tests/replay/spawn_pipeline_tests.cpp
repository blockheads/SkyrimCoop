#include <catch2/catch.hpp>
#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

#include "Services/CharacterService.h"
#include "game_bridge/proc_memory.h"
#include "game_bridge/pointer_table.h"
#include "game_bridge/game_reader.h"
#include "game_bridge/tcp_client.h"

// Test helper: creates a CharacterService with no-op GameReader and TcpClient.
// ProcMemReader without Open() returns zero for all reads.
// TcpClient without Connect() silently fails all sends.
struct SpawnTestFixture
{
    ProcMemReader mem;      // fd=-1, all reads return 0
    PointerTable ptrs;
    GameReader reader{mem, ptrs};
    TcpClient client;       // socket=-1, all sends return false
    entt::registry registry;
    CharacterService svc{reader, client, registry};

    // Count RemoteComponent entities
    size_t RemoteCount()
    {
        auto view = registry.view<RemoteComponent>();
        size_t count = 0;
        for ([[maybe_unused]] auto e : view)
            ++count;
        return count;
    }

    // Count LocalComponent entities
    size_t LocalCount()
    {
        auto view = registry.view<LocalComponent>();
        size_t count = 0;
        for ([[maybe_unused]] auto e : view)
            ++count;
        return count;
    }

    // Find RemoteComponent by serverId, returns nullptr if not found
    const RemoteComponent* FindRemote(uint32_t aServerId)
    {
        auto view = registry.view<RemoteComponent>();
        for (auto entity : view)
        {
            auto& comp = view.get<RemoteComponent>(entity);
            if (comp.serverId == aServerId)
                return &comp;
        }
        return nullptr;
    }

    // Find InterpolationComponent by serverId
    const InterpolationComponent* FindInterp(uint32_t aServerId)
    {
        auto view = registry.view<RemoteComponent, InterpolationComponent>();
        for (auto entity : view)
        {
            auto& remote = view.get<RemoteComponent>(entity);
            if (remote.serverId != aServerId)
                continue;
            return &view.get<InterpolationComponent>(entity);
        }
        return nullptr;
    }
};

// =============================================================================
// [SpawnRouting] -- Tests for OnAssignCharacter spawn routing
// =============================================================================

TEST_CASE("OnAssignCharacter with formId==0 creates RemoteComponent entity", "[SpawnRouting]")
{
    SpawnTestFixture fix;

    // FormId=0 represents a player character (no existing world form)
    fix.svc.OnAssignCharacter(42, 0, {100.0f, 200.0f, 300.0f}, {1.0f, 0.5f});

    REQUIRE(fix.RemoteCount() == 1);

    auto* pRemote = fix.FindRemote(42);
    REQUIRE(pRemote != nullptr);
    CHECK(pRemote->serverId == 42);
    CHECK(pRemote->formId == 0);
    // gamePtr is 0 because PointerTable is empty (no game process)
    CHECK(pRemote->gamePtr == 0);
}

TEST_CASE("OnAssignCharacter with non-zero formId creates RemoteComponent with formId", "[SpawnRouting]")
{
    SpawnTestFixture fix;

    // Non-zero FormId = existing NPC form (e.g., Riverwood guard 0x7)
    fix.svc.OnAssignCharacter(3, 0x7, {500.0f, 600.0f, 700.0f}, {0.0f, 3.14f});

    REQUIRE(fix.RemoteCount() == 1);

    auto* pRemote = fix.FindRemote(3);
    REQUIRE(pRemote != nullptr);
    CHECK(pRemote->serverId == 3);
    CHECK(pRemote->formId == 0x7);
}

TEST_CASE("OnAssignCharacter creates InterpolationComponent with initial position", "[SpawnRouting]")
{
    SpawnTestFixture fix;

    const glm::vec3 pos{100.0f, 200.0f, 300.0f};
    const glm::vec2 rot{1.0f, 0.5f};
    fix.svc.OnAssignCharacter(42, 0, pos, rot);

    auto* pInterp = fix.FindInterp(42);
    REQUIRE(pInterp != nullptr);
    CHECK(pInterp->targetPos.x == Approx(100.0f));
    CHECK(pInterp->targetPos.y == Approx(200.0f));
    CHECK(pInterp->targetPos.z == Approx(300.0f));
    CHECK(pInterp->currentPos.x == Approx(100.0f));
    CHECK(pInterp->currentPos.y == Approx(200.0f));
    CHECK(pInterp->currentPos.z == Approx(300.0f));
    CHECK(pInterp->targetRot.x == Approx(1.0f));
    CHECK(pInterp->targetRot.y == Approx(0.5f));
    CHECK(pInterp->initialized == true);
}

TEST_CASE("Duplicate serverId in OnAssignCharacter updates existing entity, does not create second", "[SpawnRouting]")
{
    SpawnTestFixture fix;

    // First assignment
    fix.svc.OnAssignCharacter(42, 0, {100.0f, 200.0f, 300.0f}, {1.0f, 0.5f});
    REQUIRE(fix.RemoteCount() == 1);

    // Second assignment with same serverId but different formId
    fix.svc.OnAssignCharacter(42, 0x14, {500.0f, 600.0f, 700.0f}, {0.0f, 1.0f});

    // Should still have only 1 remote entity (updated, not duplicated)
    REQUIRE(fix.RemoteCount() == 1);

    auto* pRemote = fix.FindRemote(42);
    REQUIRE(pRemote != nullptr);
    CHECK(pRemote->formId == 0x14);
}

TEST_CASE("Multiple different serverIds create separate entities", "[SpawnRouting]")
{
    SpawnTestFixture fix;

    fix.svc.OnAssignCharacter(1, 0, {0,0,0}, {0,0});
    fix.svc.OnAssignCharacter(2, 0x7, {100,100,100}, {0,0});
    fix.svc.OnAssignCharacter(3, 0x14, {200,200,200}, {0,0});

    REQUIRE(fix.RemoteCount() == 3);
    CHECK(fix.FindRemote(1) != nullptr);
    CHECK(fix.FindRemote(2) != nullptr);
    CHECK(fix.FindRemote(3) != nullptr);
}

// =============================================================================
// [Reconciliation] -- Tests for position updates and entity removal
// =============================================================================

TEST_CASE("OnReferencesMoveRequest updates interpolation target for known serverId", "[Reconciliation]")
{
    SpawnTestFixture fix;

    // Setup: create remote entity
    fix.svc.OnAssignCharacter(42, 0, {100.0f, 200.0f, 300.0f}, {0.0f, 0.0f});

    // Update position
    fix.svc.OnReferencesMoveRequest(42, {500.0f, 600.0f, 700.0f}, {1.5f, 2.0f});

    auto* pInterp = fix.FindInterp(42);
    REQUIRE(pInterp != nullptr);
    CHECK(pInterp->targetPos.x == Approx(500.0f));
    CHECK(pInterp->targetPos.y == Approx(600.0f));
    CHECK(pInterp->targetPos.z == Approx(700.0f));
    CHECK(pInterp->targetRot.x == Approx(1.5f));
    CHECK(pInterp->targetRot.y == Approx(2.0f));
}

TEST_CASE("OnReferencesMoveRequest for unknown serverId does not crash or create entity", "[Reconciliation]")
{
    SpawnTestFixture fix;

    // No entities exist -- move request for unknown serverId should be harmless
    fix.svc.OnReferencesMoveRequest(99, {100.0f, 200.0f, 300.0f}, {0.0f, 0.0f});

    CHECK(fix.RemoteCount() == 0);
}

TEST_CASE("OnRemoveCharacter removes entity for known serverId (orphan removal)", "[Reconciliation]")
{
    SpawnTestFixture fix;

    // Setup: create two remote entities
    fix.svc.OnAssignCharacter(10, 0, {0,0,0}, {0,0});
    fix.svc.OnAssignCharacter(20, 0x7, {100,100,100}, {0,0});
    REQUIRE(fix.RemoteCount() == 2);

    // Remove one -- simulates server reconciliation removing an orphan
    fix.svc.OnRemoveCharacter(10);

    REQUIRE(fix.RemoteCount() == 1);
    CHECK(fix.FindRemote(10) == nullptr);
    CHECK(fix.FindRemote(20) != nullptr);
}

TEST_CASE("OnRemoveCharacter for unknown serverId does not crash", "[Reconciliation]")
{
    SpawnTestFixture fix;

    fix.svc.OnAssignCharacter(10, 0, {0,0,0}, {0,0});
    REQUIRE(fix.RemoteCount() == 1);

    // Remove non-existent serverId -- should log warning but not crash
    fix.svc.OnRemoveCharacter(999);

    // Original entity still exists
    REQUIRE(fix.RemoteCount() == 1);
    CHECK(fix.FindRemote(10) != nullptr);
}

TEST_CASE("Position correction: move then re-assign resets interpolation", "[Reconciliation]")
{
    SpawnTestFixture fix;

    // Create entity and move it
    fix.svc.OnAssignCharacter(42, 0, {100,200,300}, {0,0});
    fix.svc.OnReferencesMoveRequest(42, {500,600,700}, {1,1});

    // Re-assign (same serverId) resets position
    fix.svc.OnAssignCharacter(42, 0, {0,0,0}, {0,0});

    auto* pInterp = fix.FindInterp(42);
    REQUIRE(pInterp != nullptr);
    CHECK(pInterp->currentPos.x == Approx(0.0f));
    CHECK(pInterp->currentPos.y == Approx(0.0f));
    CHECK(pInterp->currentPos.z == Approx(0.0f));
}

// =============================================================================
// [Disconnect] -- Tests for disconnect cleanup behavior
// =============================================================================

TEST_CASE("Removing all remote entities via OnRemoveCharacter clears registry", "[Disconnect]")
{
    SpawnTestFixture fix;

    // Create multiple remote entities
    fix.svc.OnAssignCharacter(1, 0, {0,0,0}, {0,0});
    fix.svc.OnAssignCharacter(2, 0x7, {100,100,100}, {0,0});
    fix.svc.OnAssignCharacter(3, 0x14, {200,200,200}, {0,0});
    REQUIRE(fix.RemoteCount() == 3);

    // Remove all -- simulates disconnect cleanup
    fix.svc.OnRemoveCharacter(1);
    fix.svc.OnRemoveCharacter(2);
    fix.svc.OnRemoveCharacter(3);

    CHECK(fix.RemoteCount() == 0);
}

TEST_CASE("After removing all remote entities, new assignments work correctly", "[Disconnect]")
{
    SpawnTestFixture fix;

    // Create and remove
    fix.svc.OnAssignCharacter(1, 0, {0,0,0}, {0,0});
    fix.svc.OnAssignCharacter(2, 0x7, {100,100,100}, {0,0});
    fix.svc.OnRemoveCharacter(1);
    fix.svc.OnRemoveCharacter(2);
    CHECK(fix.RemoteCount() == 0);

    // New assignment after cleanup
    fix.svc.OnAssignCharacter(10, 0x21, {300,300,300}, {0,0});
    REQUIRE(fix.RemoteCount() == 1);

    auto* pRemote = fix.FindRemote(10);
    REQUIRE(pRemote != nullptr);
    CHECK(pRemote->serverId == 10);
    CHECK(pRemote->formId == 0x21);
}

TEST_CASE("Disconnect cleanup does not affect local entities", "[Disconnect]")
{
    SpawnTestFixture fix;

    // Manually create a local entity to simulate local player tracking
    auto localEntity = fix.registry.create();
    fix.registry.emplace<LocalComponent>(localEntity, uint32_t{0x14}, uint64_t{0xDEAD}, false, false);

    // Create remote entities
    fix.svc.OnAssignCharacter(1, 0, {0,0,0}, {0,0});
    fix.svc.OnAssignCharacter(2, 0x7, {100,100,100}, {0,0});
    REQUIRE(fix.RemoteCount() == 2);
    REQUIRE(fix.LocalCount() == 1);

    // Remove all remotes
    fix.svc.OnRemoveCharacter(1);
    fix.svc.OnRemoveCharacter(2);

    // Remote entities gone, local entity still present
    CHECK(fix.RemoteCount() == 0);
    CHECK(fix.LocalCount() == 1);
}
