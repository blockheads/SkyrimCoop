
#include "AI/Movement/PlayerControls.h"

PlayerControls* PlayerControls::GetInstance()
{
    POINTER_SKYRIMSE(PlayerControls*, s_instance, 400864);
    return *(s_instance.Get());
}

void PlayerControls::SetEnabled(bool abFlag)
{
    using TSetEnabled = void(PlayerControls*, bool);
    POINTER_SKYRIMSE(TSetEnabled, setEnabled, 42345);
    setEnabled(this, abFlag);
}

void PlayerControls::SetCamSwitch(bool aSet) noexcept
{
    Data.remapMode = aSet;
}

BSInputEnableManager* BSInputEnableManager::Get()
{
    POINTER_SKYRIMSE(BSInputEnableManager*, s_instance, 400863);
    return *(s_instance.Get());
}

void BSInputEnableManager::EnableOtherEvent(int32_t aFlags, bool aEnable, bool aUnk2)
{
    using TSetEnabled = void(BSInputEnableManager*, int32_t, bool, bool);
    POINTER_SKYRIMSE(TSetEnabled, setEnabled, 68545);
    setEnabled(this, aFlags, aEnable, aUnk2);
}

// static Hooks::Initializer s_Init([]() { Hooks::Put<uint8_t>(0x14072E490, 0xCC); });
