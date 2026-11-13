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
        reads.Ref.Bodies[0]->Read.LinearVelocity = {300.f, 0.f, 0.f};
        reads.Ref.Bodies[0]->Read.AngularVelocity = {0.f, 4.f, 0.f};
        const auto cRising = physics.Take();
        CHECK(cRising.Rested.empty());
        REQUIRE(cRising.Awake.size() == 1);
        // Driven by its animation, shaking step to step: a pose, with no velocity to guess onward.
        CHECK(cRising.Awake[0].Read[0].LinearVelocity == glm::vec3{});
        CHECK(cRising.Awake[0].Read[0].AngularVelocity == glm::vec3{});
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

namespace
{
// A reference woken in place (physics.md, "Woken still"): its bodies awake, each 3D on its body.
SceneReads WokenInPlace(size_t aBodies)
{
    SceneReads reads;
    reads.Ref.Bodies.clear();
    for (size_t i = 0; i < aBodies; ++i)
    {
        IBodyReads::Body body;
        body.Read.Dynamic = true;
        body.Read.Active = true;
        body.Read.Position = {10.f * static_cast<float>(i), 0.f, 80.f};
        body.Node = std::pair(body.Read.Position, body.Read.Rotation);
        reads.Ref.Bodies.push_back(body);
    }
    reads.Ref.Position = reads.Ref.Bodies[0]->Read.Position;
    return reads;
}
} // namespace

TEST_CASE("A reference woken in place sends no frames, and its rest when it sleeps", "[physics][woken-still]")
{
    auto reads = WokenInPlace(2);
    PhysicsBodies physics(reads);
    CHECK(physics.Take().Awake.empty());
    CHECK(physics.Take().Awake.empty());
    for (auto& body : reads.Ref.Bodies)
        body->Read.Active = false;
    const auto cAsleep = physics.Take();
    REQUIRE(cAsleep.Rested.size() == 1);
    CHECK(cAsleep.Rested[0].Read.size() == 2);
}

TEST_CASE("Either body of a woken reference leaving where it woke sends the reference's frames", "[physics][woken-still]")
{
    auto reads = WokenInPlace(2);
    PhysicsBodies physics(reads);
    CHECK(physics.Take().Awake.empty());
    reads.Ref.Bodies[1]->Read.Position.x += 3.f;
    reads.Ref.Bodies[1]->Node->first = reads.Ref.Bodies[1]->Read.Position; // the engine followed it
    const auto cMoved = physics.Take();
    REQUIRE(cMoved.Awake.size() == 1);
    CHECK(cMoved.Awake[0].Read.size() == 2);
    THEN("moved stays moved, back in place or not")
    {
        reads.Ref.Bodies[1]->Read.Position.x -= 3.f;
        reads.Ref.Bodies[1]->Node->first = reads.Ref.Bodies[1]->Read.Position;
        CHECK(physics.Take().Awake.size() == 1);
    }
}

TEST_CASE("A woken reference the host's player then grabs sends its frames at once", "[physics][woken-still]")
{
    auto reads = WokenInPlace(1);
    PhysicsBodies physics(reads);
    CHECK(physics.Take().Awake.empty());
    reads.Holds = 42;
    CHECK(physics.Take().Awake.size() == 1);
    THEN("held still and asleep, still the hand's: frames, no rest")
    {
        reads.Ref.Bodies[0]->Read.Active = false;
        const auto cHeld = physics.Take();
        CHECK(cHeld.Awake.size() == 1);
        CHECK(cHeld.Rested.empty());
    }
}
