#include <Forms/TESFaction.h>
#include <VersionDb.h>

void TESFaction::SetPlayerEnemy(bool aEnemy) noexcept
{
    using TSetPlayerEnemy = void(TESFaction*, bool);
    POINTER_SKYRIMSE(TSetPlayerEnemy, s_setPlayerEnemy, 24505);
    s_setPlayerEnemy(this, aEnemy);
}
