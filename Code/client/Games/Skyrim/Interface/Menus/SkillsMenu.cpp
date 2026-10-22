
// The game calls it statsmenu.
#include <Interface/Menus/SkillsMenu.h>
#include <HookSwitch.h>

static Hooks::Initializer s_skillsMenuInit(
    []()
    {
        const HookGate cGate(__FILE__);
        // https://github.com/Vermunds/SkyrimSoulsRE/blob/master/src/Menus/StatsMenuEx.cpp
        // Hoooks from souls RE
        // Fix for menu not appearing
        VersionDbPtr<uint8_t> ProcessMessage(52510);
        Hooks::Nop(ProcessMessage.Get() + 0x84E, 6);
        // Prevent setting kFreezeFrameBackground flag
        Hooks::Nop(ProcessMessage.Get() + 0xA10, 4);
        // Keep the menu updated
        Hooks::Nop(ProcessMessage.Get() + 0x1040, 2);

        // Fix for controls not working
        VersionDbPtr<uint8_t> controlPatch(52518);
        Hooks::Nop(controlPatch.Get() + 0x46, 4);
        Hooks::Nop(controlPatch.Get() + 0x4A, 2);
    });
