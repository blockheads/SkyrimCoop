// HookUpdateAI's and HookLoad3D's rule (copy-contract.md, "A guest's own NPCs"): a guest holds every actor its engine
// runs but its player, the copies and what it claimed in this session.
#include <catch2/catch.hpp>

#include <Games/GuestNpcs.h>

TEST_CASE("Out of a guest session nothing is held", "[GuestNpcs]")
{
    CHECK_FALSE(GuestNpcs::Held(0, false, false, 0));
    CHECK_FALSE(GuestNpcs::Held(0, false, false, 3));
}

TEST_CASE("A guest holds its own NPCs, never its player or a copy", "[GuestNpcs]")
{
    constexpr uint32_t cSession = 2;
    CHECK(GuestNpcs::Held(cSession, false, false, 0));
    CHECK_FALSE(GuestNpcs::Held(cSession, true, false, 0));
    CHECK_FALSE(GuestNpcs::Held(cSession, false, true, 0));
}

TEST_CASE("A horse a guest took this session is its own; one it took in an earlier session is held", "[GuestNpcs]")
{
    CHECK_FALSE(GuestNpcs::Held(2, false, false, 2));
    CHECK(GuestNpcs::Held(2, false, false, 1));
}

TEST_CASE("Each guest session is a new one, none out of a session", "[GuestNpcs]")
{
    GuestNpcs::SetHolding(true);
    const uint32_t cFirst = GuestNpcs::Session();
    GuestNpcs::SetHolding(false);
    CHECK(GuestNpcs::Session() == 0);
    GuestNpcs::SetHolding(true);
    CHECK(GuestNpcs::Session() != 0);
    CHECK(GuestNpcs::Session() != cFirst);
    GuestNpcs::SetHolding(false);
}
