#include "MagicCaster.h"
#include <VersionDb.h>

bool MagicCaster::CastSpell(MagicItem* apSpell, TESObjectREFR* apDesiredTarget, bool abLoadCast) noexcept
{
    using TCastSpell = bool(MagicCaster*, MagicItem*, TESObjectREFR*, bool);

    POINTER_SKYRIMSE(TCastSpell, s_castSpell, 34401);

    return s_castSpell(this, apSpell, apDesiredTarget, abLoadCast);
}

void MagicCaster::InterruptCast() noexcept
{
    using TInterruptCast = void(MagicCaster*);

    POINTER_SKYRIMSE(TInterruptCast, s_interruptCast, 34408);

    s_interruptCast(this);
}
