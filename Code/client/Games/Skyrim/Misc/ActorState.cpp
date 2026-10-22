#include "ActorState.h"

bool ActorState::SetWeaponDrawn(bool aDraw) noexcept
{
    using TSetWeaponState = bool(ActorState*, bool aDraw);
    POINTER_SKYRIMSE(TSetWeaponState, setWeaponState, 38979);

    return setWeaponState(this, aDraw);
}
