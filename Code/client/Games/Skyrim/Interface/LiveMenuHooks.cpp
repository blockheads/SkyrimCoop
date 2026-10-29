#include <HookSwitch.h>
#include <Hooks/Initializer.h>
#include <Hooks/FunctionHook.h>
#include <Actor.h>
#include <Interface/IMenu.h>
#include <Interface/MenuAutoClose.h>
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

// A container's or merchant's live menu closes when its target goes, dies or leaves (Skyrim Souls RE's
// AutoCloseManager): watched from the menu's kShow, checked each frame (UpdatePlayer; barter gets no kUpdate).
using TMenuProcess = UI_MESSAGE_RESULTS(IMenu*, UIMessage&);

struct AutoClosed
{
    const char* Name;
    uint32_t (*TargetHandle)();
    TMenuProcess* Real{};
    MenuAutoClose::Watch Watch{};
    bool Watching{};
};

MenuAutoClose::Target TargetOf(uint32_t aHandle) noexcept
{
    TESObjectREFR* pTarget = aHandle ? TESObjectREFR::GetByHandle(aHandle) : nullptr;
    const auto* pPlayer = PlayerCharacter::Get();
    if (!pTarget || !pPlayer)
        return {.Gone = true};
    MenuAutoClose::Target target{.Gone = pTarget->IsDisabled() || pTarget->IsDeleted()};
    if (auto* pActor = Cast<Actor>(pTarget))
    {
        target.Actor = true;
        target.Dead = pActor->IsDead();
    }
    const float cX = pTarget->position.x - pPlayer->position.x, cY = pTarget->position.y - pPlayer->position.y,
                cZ = pTarget->position.z - pPlayer->position.z;
    target.Distance = std::sqrt(cX * cX + cY * cY + cZ * cZ);
    return target;
}

UI_MESSAGE_RESULTS Watched(AutoClosed& aMenu, IMenu* apMenu, UIMessage& aMessage)
{
    if (aMessage.eType == UIMessage::kShow)
    {
        aMenu.Watching = InSession();
        if (aMenu.Watching)
            aMenu.Watch = MenuAutoClose::Open(TargetOf(aMenu.TargetHandle()));
    }
    else if (aMessage.eType == UIMessage::kHide || aMessage.eType == UIMessage::kForceHide)
        aMenu.Watching = false;
    return aMenu.Real(apMenu, aMessage);
}

void CheckWatched(AutoClosed& aMenu) noexcept
{
    if (!aMenu.Watching || !InSession())
        return;
    if (const auto cWhy = MenuAutoClose::Check(aMenu.Watch, TargetOf(aMenu.TargetHandle())); !cWhy.empty())
    {
        spdlog::info("LiveMenus: closing {}: {}", aMenu.Name, cWhy);
        aMenu.Watching = false;
        if (auto* pQueue = UIMessageQueue::Get())
            pQueue->AddMessage(aMenu.Name, UIMessage::kHide, nullptr);
    }
}

uint32_t ContainerTarget()
{
    POINTER_SKYRIMSE(uint32_t, s_handle, 405962); // ContainerMenu's target (CommonLibSSE-NG GetTargetRefHandle)
    return *s_handle.Get();
}

uint32_t BarterTarget()
{
    POINTER_SKYRIMSE(uint32_t, s_handle, 405823); // BarterMenu's target
    return *s_handle.Get();
}

AutoClosed s_container{"ContainerMenu", &ContainerTarget};
AutoClosed s_barter{"BarterMenu", &BarterTarget};

UI_MESSAGE_RESULTS ContainerProcess(IMenu* apMenu, UIMessage& aMessage)
{
    return Watched(s_container, apMenu, aMessage);
}

UI_MESSAGE_RESULTS BarterProcess(IMenu* apMenu, UIMessage& aMessage)
{
    return Watched(s_barter, apMenu, aMessage);
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
    CheckWatched(s_container);
    CheckWatched(s_barter);
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

    // ProcessMessage, vtable slot 4 (ContainerMenu 215061, BarterMenu 214926).
    HOOK_ID(51126, &s_container.Real, ContainerProcess);
    HOOK_ID(50945, &s_barter.Real, BarterProcess);

    VersionDbPtr<uint8_t> bookProcess(51049);
    Hooks::SwapCall(bookProcess.Get() + 0x6F, RealBookMenuMode, &BookMenuMode);
});
} // namespace
