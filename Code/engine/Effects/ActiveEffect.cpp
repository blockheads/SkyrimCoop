#include <Effects/ActiveEffect.h>

void ActiveEffect::Dispel(bool aForce) noexcept
{
    POINTER_SKYRIMSE(void(ActiveEffect*, bool), s_dispel, 34061);
    s_dispel(this, aForce);
}
