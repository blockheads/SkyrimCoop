// Traps' rules (docs/design/traps.md): whose a contact, a hit, a world's effect and an unseen animation are.
#include <catch2/catch.hpp>

#include <Games/Skyrim/TrapRules.h>
#include <Messages/Payloads.h>
#include <Papyrus/PartyRules.h>

using namespace TrapRules;
using enum HitRules::Party;

TEST_CASE("A trap's contact is judged by the touched player's own game", "[TrapRules]")
{
    CHECK(OfContact(false, false, LocalPlayer, false) == Contact::Raise); // single player
    CHECK(OfContact(true, true, LocalPlayer, false) == Contact::Raise);
    CHECK(OfContact(true, true, Local, false) == Contact::Raise);
    CHECK(OfContact(true, true, OtherPlayer, false) == Contact::Drop);
    CHECK(OfContact(true, true, OtherPlayer, true) == Contact::Raise); // the guest's request
    CHECK(OfContact(true, false, LocalPlayer, false) == Contact::AskHost);
    CHECK(OfContact(true, false, OtherPlayer, false) == Contact::Drop);
    CHECK(OfContact(true, false, Copy, false) == Contact::Drop);
}

TEST_CASE("A trap's hit is taken by its target's own game", "[TrapRules]")
{
    CHECK(OfHit(false, false, LocalPlayer) == Hit::Apply);
    CHECK(OfHit(true, true, LocalPlayer) == Hit::Apply);
    CHECK(OfHit(true, true, Local) == Hit::Apply);
    CHECK(OfHit(true, true, OtherPlayer) == Hit::AskOwner);
    CHECK(OfHit(true, true, Copy) == Hit::Drop); // an NPC a guest holds (its ridden horse)
    CHECK(OfHit(true, false, OtherPlayer) == Hit::Drop);
    CHECK(OfHit(true, false, LocalPlayer) == Hit::Apply);
}

TEST_CASE("On a guest its own copy of a trap's cast applies nothing to its player; an explosion's or hazard's does", "[TrapRules]")
{
    CHECK_FALSE(AppliesWorldEffect(true, LocalPlayer, true, false)); // the trap's Spell.Cast: the host's asks
    CHECK(AppliesWorldEffect(true, LocalPlayer, false, false));      // no caster: its own copy of the explosion
    CHECK(AppliesWorldEffect(true, LocalPlayer, true, true));        // its own script's
    CHECK(AppliesWorldEffect(false, LocalPlayer, true, false));
    CHECK(AppliesWorldEffect(true, Copy, true, false));
}

TEST_CASE("A world cast of an effect already on the actor is skipped; a player's is not", "[TrapRules]")
{
    CHECK(SkipsWorldEffect(true, true));
    CHECK_FALSE(SkipsWorldEffect(true, false));
    CHECK_FALSE(SkipsWorldEffect(false, true));
}

TEST_CASE("A stop contact carries no vectors: its event leaves them stack scratch", "[TrapRules]")
{
    CHECK(CarriesVectors(Payloads::TrapContact::kStart));
    CHECK(CarriesVectors(Payloads::TrapContact::kHit));
    CHECK_FALSE(CarriesVectors(Payloads::TrapContact::kStop));
}

TEST_CASE("An animation repeated at once on an object with no 3D is sent once; a later repeat is sent", "[TrapRules]")
{
    using namespace std::chrono_literals;
    CHECK(SendsAnimation(true, true, 0ms));
    CHECK(SendsAnimation(false, false, 0ms));
    CHECK_FALSE(SendsAnimation(false, true, 30ms)); // the script's loop
    CHECK(SendsAnimation(false, true, 5s));         // the trap fires again
}

TEST_CASE("An object's cast on the host is shown on every game; an actor's or a guest's own is not", "[TrapRules]")
{
    using namespace PartyRules;
    const Reach cCast{.Kind = Call::kCast};
    CHECK(RouteOf({.Reach = cCast, .Hosting = true, .ObjectCaster = true}) == Routing{.To = Destination::kEveryGame});
    CHECK(RouteOf({.Reach = cCast, .Cause = 0xFF000801, .GuestCause = true, .Hosting = true, .ObjectCaster = true}) == Routing{.To = Destination::kEveryGame});
    CHECK(RouteOf({.Reach = cCast, .Hosting = true}) == Routing{});
    CHECK(RouteOf({.Reach = cCast, .Guest = true, .ObjectCaster = true}) == Routing{});
    CHECK(RouteOf({.Reach = cCast, .Cause = kMirrored | 1, .ObjectCaster = true}) == Routing{});
}
