#include <Bridges/PhysicsBodies.h>
#include <MainLoopTasks.h>
#include <catch2/catch.hpp>

namespace
{
struct SceneReads final : IBodyReads
{
    Reference Ref;
    bool Moved = true;
    size_t Writes{};

    SceneReads()
    {
        Body body;
        body.Read.Dynamic = true;
        body.Read.Position = {20.f, 0.f, 0.f};
        body.Node = std::pair(glm::vec3{}, glm::quat(1.f, 0.f, 0.f, 0.f));
        Ref.Bodies.push_back(body);
    }
    std::vector<uint32_t> TakeMovedReferences() noexcept override { return std::exchange(Moved, false) ? std::vector<uint32_t>{42} : std::vector<uint32_t>{}; }
    uint32_t PlayerInRagdoll() noexcept override { return 0; }
    uint32_t Holds = 0;
    uint32_t PlayerHolds() noexcept override { return Holds; }
    std::optional<Reference> ReadReference(uint32_t, bool = true) noexcept override { return Ref; }
    void WriteNode(uint32_t, uint8_t aIndex) noexcept override
    {
        ++Writes;
        auto& body = *Ref.Bodies.at(aIndex);
        Ref.Position = body.Read.Position;
        body.Node = std::pair(body.Read.Position, body.Read.Rotation);
    }
};
}

TEST_CASE("Physics snapshots leave scene writes for the main loop", "[body-scene-phase]")
{
    SceneReads reads;
    reads.Ref.Bodies[0]->Read.Active = true;
    PhysicsBodies physics(reads);
    REQUIRE(physics.Take().Awake.size() == 1);
    CHECK(reads.Writes == 0);
    // The engine may follow it itself before the queued repair runs.
    SECTION("the body still needs its node")
    {
        MainLoopTasks::Run();
        CHECK(reads.Writes == 1);
        CHECK(reads.Ref.Position.x == 20.f);
    }
    SECTION("an engine-followed node keeps its contacts")
    {
        reads.Ref.Bodies[0]->Node->first = reads.Ref.Bodies[0]->Read.Position;
        MainLoopTasks::Run();
        CHECK(reads.Writes == 0);
    }
}

TEST_CASE("A sleeping body's move row waits for its deferred node repair", "[body-scene-phase]")
{
    SceneReads reads;
    PhysicsBodies physics(reads);
    const auto rest = physics.Take().Rested;
    REQUIRE(rest.size() == 1);
    CHECK(rest[0].Read[0].Position.x == 20.f);
    CHECK(physics.TakeRested().empty());
    MainLoopTasks::Run();
    CHECK(reads.Ref.Position.x == 20.f);
    CHECK(physics.TakeRested() == std::vector<uint32_t>{42});
    CHECK(physics.TakeRested().empty());
}

TEST_CASE("An unloaded reference cancels its queued node repair and move row", "[body-scene-phase]")
{
    SceneReads reads;
    PhysicsBodies physics(reads);
    REQUIRE(physics.Take().Rested.size() == 1);
    physics.Forget(42);
    MainLoopTasks::Run();
    CHECK(reads.Writes == 0);
    CHECK(physics.TakeRested().empty());
}

TEST_CASE("Leaving a physics session drops its queued scene writes", "[body-scene-phase]")
{
    SceneReads reads;
    {
        PhysicsBodies physics(reads);
        REQUIRE(physics.Take().Rested.size() == 1);
    }
    MainLoopTasks::Run();
    CHECK(reads.Writes == 0);
}

TEST_CASE("A body this game's player holds still is not at rest: Havok sleeps it, a hand holds it up", "[body-scene-phase]")
{
    // host-holds-into-npc: pressed against Lydia, the host's bucket slept; sent as a rest, the guest's fell from the
    // hand, was put back, and fell again (2026-11-02 report).
    SceneReads reads;
    reads.Holds = 42;
    PhysicsBodies physics(reads);
    const auto cHeld = physics.Take();
    CHECK(cHeld.Rested.empty());
    REQUIRE(cHeld.Awake.size() == 1);
    CHECK(physics.Take().Awake.size() == 1); // still held, still asleep: still sent
    reads.Holds = 0;
    CHECK(physics.Take().Rested.size() == 1); // let go: its rest
}

TEST_CASE("An actor getting up still sends its ragdoll, without knocking its copies down, and rests once it stands", "[body-scene-phase][rising]")
{
    // knockdown-lies-as-host (duo, 2026-11-11): getting up, a ragdoll's bodies are still simulated (58291), so each
    // game's room moved them its own way; the owner stopped sending at its get-up's start.
    SceneReads reads;
    reads.Ref.Actor = true;
    reads.Ref.Bodies[0]->Node.reset();
    reads.Ref.Bodies[0]->Read.Active = true;
    PhysicsBodies physics(reads);
    const auto cLying = physics.Take();
    REQUIRE(cLying.Awake.size() == 1);
    CHECK(cLying.Awake[0].Read[0].Knocked);

    SECTION("lying still it rests once and is kept: its get-up need not mark it moved again")
    {
        reads.Ref.Bodies[0]->Read.Active = false;
        CHECK(physics.Take().Rested.size() == 1);
        const auto cStill = physics.Take();
        CHECK(cStill.Rested.empty());
        CHECK(cStill.Awake.empty());
        reads.Ref.Rising = true;
        reads.Ref.Bodies[0]->Read.Active = true;
        CHECK(physics.Take().Awake.size() == 1);
    }
    SECTION("getting up its frames go on, as a getting-up owner's")
    {
        reads.Ref.Rising = true;
        reads.Ref.Bodies[0]->Read.Position = {20.f, 0.f, 30.f};
        const auto cRising = physics.Take();
        CHECK(cRising.Rested.empty());
        REQUIRE(cRising.Awake.size() == 1);
        CHECK_FALSE(cRising.Awake[0].Read[0].Knocked); // a late one must not take a copy already up down again
        // Standing: its bodies have left the world, so its last frame is its rest.
        reads.Ref.Rising = false;
        reads.Ref.Up = true;
        reads.Ref.Bodies[0].reset();
        const auto cUp = physics.Take();
        CHECK(cUp.Awake.empty());
        REQUIRE(cUp.Rested.size() == 1);
        CHECK(cUp.Rested[0].Read[0].Position.z == 30.f);
        CHECK_FALSE(cUp.Rested[0].Read[0].Knocked);
        CHECK(physics.Take().Rested.empty());
    }
    SECTION("a corpse asleep is done with")
    {
        reads.Ref.Dead = true;
        reads.Ref.Bodies[0]->Read.Active = false;
        CHECK(physics.Take().Rested.size() == 1);
        CHECK(physics.Take().Rested.empty());
        reads.Ref.Bodies[0]->Read.Active = true; // not read again until physics marks it moved
        CHECK(physics.Take().Awake.empty());
    }
}
