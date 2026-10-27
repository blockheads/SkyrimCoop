#include <HookSwitch.h>
#include <Hooks/Initializer.h>
#include <Hooks/FunctionHook.h>
#include <Interface/IMenu.h>
#include <Interface/Menus/MapMenu.h>
#include <Interface/UI.h>
#include <PlayerCharacter.h>
#include <NetImmerse/NiNode.h>
#include <World.h>

namespace
{
bool InSession() noexcept
{
    return entt::locator<World>::has_value() && World::Get().GetTransport().IsConnected();
}

bool MapRuns() noexcept
{
    UI* pUI = UI::Get();
    const auto* cpMap = pUI && pUI->GetMenuOpen("MapMenu") ? pUI->FindMenuByName("MapMenu") : nullptr;
    return InSession() && cpMap && !cpMap->PausesGame();
}

// Main::UpdatePlayer (36581+0x7A) only tests this result for zero; the map still needs the player update.
uintptr_t HasPlayerModel(PlayerCharacter* apPlayer)
{
    return apPlayer->GetNiNode() || MapRuns();
}

using TUpdatePlayer = uint64_t(void*);
TUpdatePlayer* RealUpdatePlayer{};
NiNode* s_hiddenModel{};
bool s_wasHidden{};

uint64_t UpdatePlayer(void* apMain)
{
    auto* pPlayer = PlayerCharacter::Get();
    auto* pModel = pPlayer ? pPlayer->firstPerson3D : nullptr;
    if (pModel && pModel == s_hiddenModel)
        pModel->flags = (pModel->flags & ~1u) | static_cast<uint32_t>(s_wasHidden);
    s_hiddenModel = nullptr;
    const uint64_t cResult = RealUpdatePlayer(apMain);
    pPlayer = PlayerCharacter::Get();
    pModel = pPlayer ? pPlayer->firstPerson3D : nullptr;
    if (pModel && MapRuns())
    {
        s_hiddenModel = pModel;
        s_wasHidden = (pModel->flags & 1) != 0;
        pModel->flags |= 1;
    }
    return cResult;
}

using TLocalMap = uint64_t(MapMenu*, bool);
TLocalMap* RealLocalMap{};
using TFreezeBackground = void(void*);
TFreezeBackground* RealFreezeBackground{};

void FreezeMapBackground(void* apState)
{
    if (!MapRuns())
        RealFreezeBackground(apState);
}

uint64_t LocalMap(MapMenu* apMenu, bool aShown)
{
    const uint64_t cResult = RealLocalMap(apMenu, aShown);
    if (InSession() && !apMenu->PausesGame())
        apMenu->ClearFlag(IMenu::kFreezeFrameBackground);
    return cResult;
}

using TMapProcess = UI_MESSAGE_RESULTS(MapMenu*, UIMessage&);
TMapProcess* RealMapProcess{};

UI_MESSAGE_RESULTS MapProcess(MapMenu* apMenu, UIMessage& aMessage)
{
    // Skyrim Souls RE: generic updates continually restart the native map-marker pick in a live map.
    if (InSession() && !apMenu->PausesGame() && aMessage.eType == UIMessage::kUpdate && aMessage.menu != "MapMenu")
        return UI_MESSAGE_RESULTS::kHandled;
    return RealMapProcess(apMenu, aMessage);
}

using TMenuMode = bool(uint32_t);
TMenuMode* RealBookMenuMode{};

bool BookMenuMode(uint32_t aMode)
{
    // BookMenu::ProcessMessage (51049) otherwise defers creating the book's 3D until the game pauses.
    return InSession() ? false : RealBookMenuMode(aMode);
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    VersionDbPtr<uint8_t> updatePlayer(36581);
    Hooks::Nop(updatePlayer.Get() + 0x7A, 6);
    Hooks::PutCall(updatePlayer.Get() + 0x7A, &HasPlayerModel);
    HOOK_ID(36581, &RealUpdatePlayer, UpdatePlayer);

    // 1.6.1170: MapMenu's local-map updater sets the background flag and freezes the render frame.
    VersionDbPtr<uint8_t> localMap(53112);
    Hooks::SwapCall(localMap.Get() + 0xC2, RealFreezeBackground, &FreezeMapBackground);
    HOOK_ID(53112, &RealLocalMap, LocalMap);
    HOOK_ID(53102, &RealMapProcess, MapProcess);

    VersionDbPtr<uint8_t> bookProcess(51049);
    Hooks::SwapCall(bookProcess.Get() + 0x6F, RealBookMenuMode, &BookMenuMode);
});
} // namespace
