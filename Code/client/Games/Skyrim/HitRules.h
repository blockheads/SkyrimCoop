#pragma once

// HookDamageActor's rule with no engine (docs/design/friendly-fire.md): what this game does with a hit it saw.
// Checked by ClientTests; the hook and GameTests' fake engine both follow it.
namespace HitRules
{
// Whose an actor in a hit is, on this game.
enum class Party
{
    None,        // no actor (a trap, a fall)
    LocalPlayer, // this game's player
    OtherPlayer, // another game's player's copy
    Local,       // this game's NPC
    Copy,        // another game's NPC's copy
};

// An object's impact (a falling rock; Actor::TakeImpactFromBody 38185, in the hit actor's own move step) is judged in
// that actor's own game, as its trap contacts are: not on another player's copy, whose step meets followed bodies.
[[nodiscard]] constexpr bool JudgesImpact(Party aHittee) noexcept
{
    return aHittee != Party::OtherPlayer;
}

enum class Route
{
    Apply,    // the owner takes it here
    AskOwner, // our player's hit on another game's actor: a request to its owner, the copy unchanged
    Mirror,   // another game's actor, hit by someone else: its own game decides, the copy unchanged
    Drop,     // a replayed swing on our player: the attacker's game already asked us
};

// aApplyingEffect: the damage comes from an effect this game applies (another game's spell on our actor among them).
// aWorldHit: a hit with no actor aggressor (an object's projectile, a causeless explosion). The host's on another
// player's copy is its verdict for that player's game; a guest's own copy of it on its player is dropped (aGuest).
[[nodiscard]] constexpr Route Of(Party aHittee, Party aHitter, bool aApplyingEffect, bool aWorldHit = false, bool aGuest = false) noexcept
{
    const bool cOurs = aHittee == Party::LocalPlayer || aHittee == Party::Local;
    if (aWorldHit && aHitter == Party::None && !aGuest && aHittee == Party::OtherPlayer)
        return Route::AskOwner;
    if (aWorldHit && aHitter == Party::None && aGuest && aHittee == Party::LocalPlayer)
        return Route::Drop;
    if (aHitter == Party::LocalPlayer)
        return cOurs ? Route::Apply : Route::AskOwner;
    if (aHitter == Party::OtherPlayer)
        return cOurs && aApplyingEffect ? Route::Apply : aHittee == Party::LocalPlayer ? Route::Drop : Route::Mirror;
    return cOurs ? Route::Apply : Route::Mirror;
}
} // namespace HitRules
