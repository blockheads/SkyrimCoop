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
    CHECK(Shut({.Life = 2}) == "life state");
    CHECK(Shut({.Life = 3}) == "life state");
    // Bleeding out (8): the gate passes, ProcessTracking takes no target.
    CHECK(Shut({.Life = 8}) == "life state");
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
    CHECK(ShutBy({.LodDistance = 2001.f, .ExtraLarge = true}) == nullptr);
    CHECK(Shut({.LodDistance = 12001.f, .ExtraLarge = true}) == "out of range");
    CHECK(Shut({.TrackingOn = false}) == "tracking off");
    // This game's player: the camera, never the range.
    CHECK(Shut({.LodDistance = -1.f, .Player = true}) == "first person");
    CHECK(ShutBy({.LodDistance = -1.f, .Player = true, .ThirdPerson = true}) == nullptr);
    CHECK(Shut({.Knock = 3, .Player = true, .ThirdPerson = true}) == "knocked");
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
    // Slot 2 alternates with slot 0 in a conversation (38009), and only there.
    CHECK(Targets(Slots{cA, 0, cB}, cSelf) == Read{cB});
    CHECK(Targets(Slots{cA, 0, cB}, cSelf, true) == Read{cB, cA});
    CHECK(Targets(Slots{cA, 0, 0, cB}, cSelf, true) == Read{cB});
    // The actor itself: no target, and it shadows what is under it (Faendal's slot 1, a player's shout).
    CHECK(Targets(Slots{0, cSelf}, cSelf).empty());
    CHECK(Targets(Slots{cA, cSelf}, cSelf).empty());
    CHECK(Targets(Slots{cSelf, 0, cB}, cSelf, true) == Read{cB});
}
