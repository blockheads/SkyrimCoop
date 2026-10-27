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

enum class Route
{
    Apply,    // the owner takes it here
    AskOwner, // our player's hit on another game's actor: a request to its owner, the copy unchanged
    Mirror,   // another game's actor, hit by someone else: its own game decides, the copy unchanged
    Drop,     // a replayed swing on our player: the attacker's game already asked us
};

// aApplyingEffect: the damage comes from an effect this game applies (another game's spell on our actor among them).
[[nodiscard]] constexpr Route Of(Party aHittee, Party aHitter, bool aApplyingEffect) noexcept
{
    const bool cOurs = aHittee == Party::LocalPlayer || aHittee == Party::Local;
    if (aHitter == Party::LocalPlayer)
        return cOurs ? Route::Apply : Route::AskOwner;
    if (aHitter == Party::OtherPlayer)
        return cOurs && aApplyingEffect ? Route::Apply : aHittee == Party::LocalPlayer ? Route::Drop : Route::Mirror;
    return cOurs ? Route::Apply : Route::Mirror;
}
} // namespace HitRules
