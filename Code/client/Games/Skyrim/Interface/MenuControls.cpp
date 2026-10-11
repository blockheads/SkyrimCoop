
#include "Interface/MenuControls.h"
#include <HookSwitch.h>

void MenuControls::SetToggle(bool b)
{
    canBeOpened = b;
}

MenuControls* MenuControls::GetInstance()
{
    POINTER_SKYRIMSE(MenuControls*, s_instance, 401263);
    return *(s_instance.Get());
}

#include <Services/TransportService.h>

static TiltedPhoques::Initializer s_init(
    []() {
        const HookGate cGate(__FILE__);

    });
