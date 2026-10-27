// HookDamageActor's rule (docs/design/friendly-fire.md): the owner applies a hit, our player's hit on another game's
// actor is a request to its owner, and friendly fire reaches another player like any NPC's hit.
#include <catch2/catch.hpp>

#include <Games/Skyrim/HitRules.h>

using namespace HitRules;
using enum Party;

TEST_CASE("Our player's hit on another player's copy asks that player's game", "[HitRules]")
{
    CHECK(Of(OtherPlayer, LocalPlayer, false) == Route::AskOwner);
    CHECK(Of(Copy, LocalPlayer, false) == Route::AskOwner);
}

TEST_CASE("Another player's spell on our player is applied; its replayed swing is not", "[HitRules]")
{
    CHECK(Of(LocalPlayer, OtherPlayer, true) == Route::Apply);
    CHECK(Of(LocalPlayer, OtherPlayer, false) == Route::Drop);
    CHECK(Of(Local, OtherPlayer, true) == Route::Apply);
    CHECK(Of(Local, OtherPlayer, false) == Route::Mirror);
}

TEST_CASE("Hits between others' actors are their games' to decide", "[HitRules]")
{
    CHECK(Of(OtherPlayer, OtherPlayer, false) == Route::Mirror);
    CHECK(Of(OtherPlayer, Copy, false) == Route::Mirror);
    CHECK(Of(Copy, OtherPlayer, true) == Route::Mirror);
    CHECK(Of(Copy, None, false) == Route::Mirror);
}

TEST_CASE("Our own actors take what the world does to them", "[HitRules]")
{
    CHECK(Of(LocalPlayer, Copy, false) == Route::Apply);
    CHECK(Of(LocalPlayer, None, false) == Route::Apply);
    CHECK(Of(Local, LocalPlayer, false) == Route::Apply);
    CHECK(Of(Local, Copy, false) == Route::Apply);
}
