#include <Games/Animation/GraphState.h>

#include <catch2/catch.hpp>

using namespace GraphState;

TEST_CASE("A graph's state crosses node by node: ids, sync, states and runtime members", "[graph-state]")
{
    Node machine;
    machine.Id = 412;
    machine.Check = Check("hkbStateMachine", "StandingBehavior");
    machine.Sync.NumSyncPoints = 2;
    machine.Sync.Points[0] = {3, 0.25f};
    machine.Sync.Points[1] = {-1, 1.5f};
    machine.Sync.LocalTime = 0.75f;
    machine.Sync.PlaybackSpeed = 1.f;
    machine.Sync.IsCyclic = true;
    machine.Activated = true;
    machine.State = std::string("\x01\x00\x02", 3);
    Node timer;
    timer.Id = 7;
    timer.Check = Check("BSTimerModifier", "IdleTimer");
    timer.Runtime = "\x10\x20";
    Node stateless; // a nested behaviour graph: its node info only
    stateless.Id = 65535;

    std::vector<Node> decoded;
    REQUIRE(Decode(Encode({machine, timer, stateless}), decoded));
    REQUIRE(decoded.size() == 3);
    CHECK(decoded[0].Id == 412);
    CHECK(decoded[0].Check == machine.Check);
    CHECK(decoded[0].Sync.NumSyncPoints == 2);
    CHECK(decoded[0].Sync.Points[1].Id == -1);
    CHECK(decoded[0].Sync.Points[1].Time == 1.5f);
    CHECK(decoded[0].Sync.LocalTime == 0.75f);
    CHECK(decoded[0].Sync.IsCyclic);
    CHECK_FALSE(decoded[0].Sync.IsMirrored);
    CHECK(decoded[0].Activated);
    REQUIRE(decoded[0].State);
    CHECK(*decoded[0].State == machine.State);
    CHECK_FALSE(decoded[1].State);
    CHECK(decoded[1].Runtime == "\x10\x20");
    CHECK(decoded[2].Id == 65535);
    CHECK(decoded[2].Runtime.empty());
}

TEST_CASE("A node's check tells another class or name apart; a cut graph state is refused", "[graph-state]")
{
    CHECK(Check("hkbStateMachine", "A") != Check("hkbStateMachine", "B"));
    CHECK(Check("hkbStateMachine", "A") != Check("hkbBlenderGenerator", "A"));
    CHECK(Check("ab", "c") != Check("a", "bc"));

    Node node;
    node.State = std::string(40, 'x');
    auto bytes = Encode({node, node});
    std::vector<Node> decoded;
    REQUIRE(Decode(bytes, decoded));
    bytes.pop_back();
    CHECK_FALSE(Decode(bytes, decoded));
    CHECK_FALSE(Decode(std::string("\xff\xff\xff\xff\x0f", 5), decoded)); // a count past the cap
}

TEST_CASE("No sync point carries a cyclic blend generator's cross-blend", "[graph-state]")
{
    // 63132's modes: following and frozen are plain members; a cross-blend (2, or 3 asked) runs on linked blenders.
    CHECK(GraphState::CarriesCyclicMode(0));
    CHECK(GraphState::CarriesCyclicMode(1));
    CHECK_FALSE(GraphState::CarriesCyclicMode(2));
    CHECK_FALSE(GraphState::CarriesCyclicMode(3));
}
