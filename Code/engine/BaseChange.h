#pragma once

#include <cstdint>

// Whether a reference's new base is recorded as its base change (CHANGE_REFR_BASEOBJECT, 0x80). A temporary base (a
// leveled actor's, built again from its ExtraLeveledCreature on load, 12095) never is: the next load of the
// reference's stored changes (TESObjectREFR::LoadGame 19529) looks the id up again, gives that NPC (the same one, or
// a newer one that took the id) to the reference, and the ExtraLeveledCreature case then destroys it as the
// reference's own, under whatever else still uses it (docs/re/crashes/2026-11-27-guest-cell-attach-null-virtual.md).
[[nodiscard]] constexpr bool RecordsBaseChange(uint32_t aBaseFormId) noexcept
{
    return aBaseFormId < 0xFF000000;
}
