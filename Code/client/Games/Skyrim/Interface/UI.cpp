#include <Games/Skyrim/Interface/IMenu.h>
#include <HookSwitch.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Misc/BSFixedString.h>
#include <ClientPch.h>

#include <World.h>

static bool g_RequestUnpauseAll{false};

UI* UI::Get()
{
    POINTER_SKYRIMSE(UI*, s_instance, 400327);
    return *s_instance.Get();
}

bool UI::GetMenuOpen(const BSFixedString& acName) const
{
    if (acName.data == nullptr)
        return false;

    using TMenuSystem_IsOpen = bool(const UI*, const BSFixedString&);
    POINTER_SKYRIMSE(TMenuSystem_IsOpen, s_isMenuOpen, 82074);

    return (s_isMenuOpen.Get())(this, acName);
}

void UI::CloseAllMenus()
{
    using TUI_CloseAll = void(const UI*);
    POINTER_SKYRIMSE(TUI_CloseAll, s_CloseAll, 82088);

    (s_CloseAll.Get())(this);
}

BSFixedString* UI::LookupMenuNameByInstance(IMenu* apMenu)
{
    for (auto& it : menuMap)
    {
        if (it.value.spMenu == apMenu)
            return &it.key;
    }
    return nullptr;
}

IMenu* UI::FindMenuByName(const BSFixedString& acName)
{
    for (const auto& it : menuMap)
    {
        if (it.key == acName.AsAscii())
            return it.value.spMenu;
    }
    return nullptr;
}

void UI::DebugLogAllMenus()
{
    for (auto& e : menuStack)
    {
        spdlog::info("Menu {}", e->uiMenuFlags);
    }
}

static void UnfreezeMenu(IMenu* apEntry)
{
    if (apEntry->PausesGame())
        apEntry->ClearFlag(IMenu::kPausesGame);

    if (apEntry->FreezesBackground())
        apEntry->ClearFlag(IMenu::kFreezeFrameBackground);

    if (apEntry->FreezesFramePause())
        apEntry->ClearFlag(IMenu::kFreezeFramePause);
}

static constexpr const char* kAllowList[] = {
    "TweenMenu",     "MagicMenu",     "StatsMenu",     "InventoryMenu", "MessageBoxMenu",
    "ContainerMenu", "FavoritesMenu", "Tutorial Menu", "Console"
    //"MapMenu", // MapMenu is disabled till we find a proper fix for first person.
    //"Journal Menu", // Journal menu, aka pause menu, is disabled until we find a fix for manual save crashing while unpaused.
};

static void* (*UI_AddToActiveQueue)(UI*, IMenu*, void*);

static void* UI_AddToActiveQueue_Hook(UI* apSelf, IMenu* apMenu, void* apFoundItem /*In reality a reference*/)
{
    // if the menu is empty we let the real function handle it.
    if (!apMenu || !World::Get().GetTransport().IsConnected())
        return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);

    // NOTE(Force): could also compare by RTTI later on...
    for (const char* item : kAllowList)
    {
        if (auto* pMenu = apSelf->FindMenuByName(item))
        {
            if (pMenu == apMenu)
                UnfreezeMenu(apMenu);
        }
    }

    return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);
}

UIMessageQueue* UIMessageQueue::Get() noexcept
{
    POINTER_SKYRIMSE(UIMessageQueue*, s_singleton, 400445);
    return *s_singleton.Get();
}

BSUIMessageData* UIMessageQueue::CreateData() noexcept
{
    using TCreate = BSUIMessageData*(const BSFixedString* apType);
    POINTER_SKYRIMSE(TCreate, s_create, 35855);
    const BSFixedString cType("BSUIMessageData");
    return s_create.Get()(&cType);
}

void UIMessageQueue::AddMessage(const BSFixedString& acMenu, UIMessage::UI_MESSAGE_TYPE aType, BSUIMessageData* apData) noexcept
{
    using TAddMessage = void(UIMessageQueue*, const BSFixedString* apMenu, uint32_t aType, void* apData);
    POINTER_SKYRIMSE(TAddMessage, s_addMessage, 13631);
    (s_addMessage.Get())(this, &acMenu, aType, apData);
}

using TCallback = void(void*, const BSFixedString*, uint32_t, void*);
static TCallback* UIMessageQueue__AddMessage_Real;

// Useful for debugging UI related issues.
void UIMessageQueue__AddMessage(void* a1, const BSFixedString* a2, UIMessage::UI_MESSAGE_TYPE a3, void* a4)
{
    spdlog::info("Adding Message {} with prio {} from {}", a2->AsAscii(), a3, fmt::ptr(_ReturnAddress()));
    UIMessageQueue__AddMessage_Real(a1, a2, a3, a4);
}

static Hooks::Initializer s_s(
    []()
    {
        const HookGate cGate(__FILE__);
        // pray that this doesnt fail!
        VersionDbPtr<uint8_t> ProcessHook(82082);
        Hooks::SwapCall(ProcessHook.Get() + 0x682, UI_AddToActiveQueue, &UI_AddToActiveQueue_Hook);

        // Ignore startup movie
        // TODO: Move me later.
        VersionDbPtr<uint8_t> MainInit(36548);
        Hooks::Put<uint8_t>(MainInit.Get() + 0xFE, 0xEB);

        // Credits to Skyrim Souls RE for this fix.
        // Allows the favorites menu to be numbered during connect.
        VersionDbPtr<uint8_t> FavoritesCanProcess(51538);
        Hooks::Put<uint16_t>(FavoritesCanProcess.Get() + 0x15, 0x9090);

        // Some experiments:
        // POINTER_SKYRIMSE(TCallback, s_start, 13631);
        // UIMessageQueue__AddMessage_Real = s_start.Get();
        // HOOK(&UIMessageQueue__AddMessage_Real, UIMessageQueue__AddMessage);

        // This kills the loading spinner
        // Hooks::Put<uint8_t>(0x1405D51C1, 0xEB);
        // Hooks::Nop(0x1405D51A2, 5);

        // use 8 threads by default!
        // Hooks::Put<uint8_t>(0x141E45770, 8);
    });
