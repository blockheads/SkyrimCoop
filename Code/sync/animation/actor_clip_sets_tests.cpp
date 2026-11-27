#include <catch2/catch.hpp>

#include <animation/ActorClipSets.h>

TEST_CASE("A graph manager's clip sets: the latest event per set, its loads sent once, gone with it", "[animation][clip-sets]")
{
    ActorClipSets sets;
    int manager = 0;
    sets.Record(&manager, 3, "IdleCounter");
    sets.Record(&manager, 3, "IdleLook");
    sets.Record(&manager, 47, "TurnToWalk");
    CHECK(sets.TakeUnsent(&manager).size() == 3);
    CHECK(sets.TakeUnsent(&manager).empty());
    sets.Record(&manager, 4, "IdleChair");
    const auto cHeld = sets.TakeHeld(&manager);
    CHECK(cHeld == std::map<uint8_t, std::string>{{3, "IdleLook"}, {4, "IdleChair"}, {47, "TurnToWalk"}});
    CHECK(sets.TakeUnsent(&manager).empty()); // its sync point named them
    sets.Forget(&manager);
    CHECK(sets.TakeHeld(&manager).empty());
}
