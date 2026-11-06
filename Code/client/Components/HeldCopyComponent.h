#pragma once

// One of this game's own references left a copy after its owner's actor went: it stays where the owner's went,
// with no AI of its own, until the owner spawns it again or the session ends (copy-contract.md).
struct HeldCopyComponent
{
    bool Hidden = false; // disabled by us: it would stand in this game's loaded area with no owner
};
