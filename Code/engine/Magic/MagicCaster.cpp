#include "Magic/MagicCaster.h"
#include <VersionDb.h>

void MagicCaster::InterruptCast() noexcept
{
    using TInterruptCast = void(MagicCaster*);

    POINTER_SKYRIMSE(TInterruptCast, s_interruptCast, 34408);

    s_interruptCast(this);
}
