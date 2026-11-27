
#include "AI/Movement/PlayerControls.h"
#include <VersionDb.h>

PlayerControls* PlayerControls::GetInstance()
{
    POINTER_SKYRIMSE(PlayerControls*, s_instance, 400864);
    return *(s_instance.Get());
}

BSInputEnableManager* BSInputEnableManager::Get()
{
    POINTER_SKYRIMSE(BSInputEnableManager*, s_instance, 400863);
    return *(s_instance.Get());
}

// static Hooks::Initializer s_Init([]() { Hooks::Put<uint8_t>(0x14072E490, 0xCC); });
