#include <catch2/catch.hpp>

#include <Diagnostics/HeadTrackRead.h>

#include <string_view>

using namespace HeadTrackRead;

namespace
{
std::string_view Shut(const Gate& acGate)
{
    const char* pReason = ShutBy(acGate);
    return pReason ? pReason : "";
}
} // namespace

TEST_CASE("The engine reads an actor's headtrack slots only with its gate open", "[headtrack]")
{
    CHECK(ShutBy({}) == nullptr);
    CHECK(Shut({.Dead = true}) == "life state");
    CHECK(Shut({.Life = 1}) == "life state");
    CHECK(Shut({.Life = 3}) == "life state");
    // Bleeding out (8), reanimated (4), recycled (5): the gate passes, and 38009 then looks along the path, not at the slot.
    CHECK(Shut({.Life = 8}) == "life state");
    CHECK(Shut({.Life = 4}) == "life state");
    CHECK(Shut({.Life = 5}) == "life state");
    CHECK(Shut({.Disabled = true}) == "disabled");
    // Restrained and essential down still track.
    CHECK(ShutBy({.Life = 6}) == nullptr);
    CHECK(ShutBy({.Life = 7}) == nullptr);
    CHECK(Shut({.Knock = 3}) == "knocked");
    CHECK(Shut({.Knock = 6}) == "knocked");
    CHECK(Shut({.SitSleep = 7}) == "asleep");
    // Sitting (3) still tracks.
    CHECK(ShutBy({.SitSleep = 3}) == nullptr);
    CHECK(Shut({.ProcessLevel = 1}) == "not high process");
    CHECK(Shut({.HasController = false}) == "out of range");
    CHECK(Shut({.LodDistance = -1.f}) == "out of range");
    CHECK(Shut({.LodDistance = 2001.f}) == "out of range");
    CHECK(ShutBy({.LodDistance = 2001.f, .Range = 12000.f}) == nullptr);
    CHECK(Shut({.LodDistance = 12001.f, .Range = 12000.f}) == "out of range");
    CHECK(Shut({.FarFromPlayer = true}) == "out of range");
    CHECK(Shut({.RaceTracks = false}) == "race");
    CHECK(Shut({.Has3D = false}) == "no 3D");
    CHECK(Shut({.TrackingOn = false}) == "tracking off");
    // This game's player: the camera, never the range.
    CHECK(Shut({.LodDistance = -1.f, .Player = true}) == "first person");
    CHECK(ShutBy({.LodDistance = -1.f, .Player = true, .ThirdPerson = true}) == nullptr);
    // Its update (40447) asks nothing else: knocked in third person, it still reads its slots.
    CHECK(ShutBy({.Knock = 3, .Player = true, .ThirdPerson = true}) == nullptr);
    CHECK(Shut({.Life = 8, .Player = true, .ThirdPerson = true}) == "life state");
}

TEST_CASE("Only the slot the engine reads counts: the highest set, and slot 0 beside slot 2", "[headtrack]")
{
    constexpr uint32_t cSelf = 0x10, cA = 0x20, cB = 0x30;
    using Slots = std::array<uint32_t, 6>;
    using Read = std::vector<uint32_t>;
    CHECK(Targets(Slots{}, cSelf).empty());
    CHECK(Targets(Slots{cA}, cSelf) == Read{cA});
    // A lower slot is shadowed until the higher one clears.
    CHECK(Targets(Slots{cA, 0, 0, cB}, cSelf) == Read{cB});
    // Slot 2 alternates with slot 0 in a scene's line (38009), and only there.
    CHECK(Targets(Slots{cA, 0, cB}, cSelf) == Read{cB});
    CHECK(Targets(Slots{cA, 0, cB}, cSelf, true) == Read{cB, cA});
    CHECK(Targets(Slots{cA, 0, 0, cB}, cSelf, true) == Read{cB});
    // The actor itself: no target, and it shadows what is under it (Faendal's slot 1, a player's shout).
    CHECK(Targets(Slots{0, cSelf}, cSelf).empty());
    CHECK(Targets(Slots{cA, cSelf}, cSelf).empty());
    CHECK(Targets(Slots{cSelf, 0, cB}, cSelf, true) == Read{cB});
    // Slot 2 on itself: whether a game alternates to slot 0 is its own, so neither reports it.
    CHECK(Targets(Slots{cA, 0, cSelf}, cSelf).empty());
    CHECK(Targets(Slots{cA, 0, cSelf}, cSelf, true).empty());
}
