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
