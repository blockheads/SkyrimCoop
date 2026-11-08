#pragma once

// One of this game's own references left a copy after its owner's actor went: it stays where the owner's went,
// with no AI of its own, until the owner spawns it again or the session ends (copy-contract.md).
struct HeldCopyComponent
{
    bool Hidden = false;       // its enables refused until its owner spawns it (GuestCopies)
    bool DisabledByUs = false; // enabled again at the session's end
};

// One of this game's references made persistent while the session lasts, so no cell unload frees it as a copy
// (GuestCopies; TESObjectCELL 19113, copy-lifecycle.md). Its own flag is back at the session's end.
struct KeptCopyComponent
{
    uint32_t FormId = 0;
    bool WasPersistent = false;
};
