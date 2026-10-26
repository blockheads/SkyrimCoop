#include <Bridges/WaitBridge.h>

#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Interface/Menus/SleepWaitMenu.h>
#include <Interface/UI.h>
#include <PlayerCharacter.h>
#include <TimeManager.h>
#include <VersionDb.h>

#include <cmath>

struct ProcessLists;

namespace
{
constexpr const char* kMenu = "Sleep/Wait Menu";
IWaitBridge::Callbacks s_callbacks;
bool s_shared{};
bool s_requested{};
bool s_driver{};
bool s_running{};
bool s_ending{};
bool s_sleeping{};
uint32_t s_hours{};
uint32_t s_nextRemainingSeconds{};

SleepWaitMenu* Menu() noexcept
{
    UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(kMenu) ? static_cast<SleepWaitMenu*>(pUI->FindMenuByName(kMenu)) : nullptr;
}

using TOkay = void(const FxDelegateArgs&);
using TCancel = void(const FxDelegateArgs&);
using TProcess = UI_MESSAGE_RESULTS(SleepWaitMenu*, UIMessage&);
using TStep = uint64_t(PlayerCharacter*);
TOkay* RealOkay{};
TCancel* RealCancel{};
TProcess* RealProcess{};
TStep* RealStep{};

void HookOkay(const FxDelegateArgs& acArgs)
{
    const auto* cpMenu = static_cast<const SleepWaitMenu*>(acArgs.menu);
    if (cpMenu && acArgs.numArgs && acArgs.args && (acArgs.args[0].type & GFxValue::kTypeMask) == GFxValue::kNumber)
    {
        const double cHours = acArgs.args[0].value.number;
        if (std::isfinite(cHours) && cHours >= 1 && cHours <= 24 && s_callbacks.Request &&
            s_callbacks.Request(static_cast<uint32_t>(cHours), cpMenu->sleeping))
        {
            s_requested = true;
            return;
        }
    }
    RealOkay(acArgs);
}

void HookCancel(const FxDelegateArgs& acArgs)
{
    if (s_callbacks.Cancel && s_callbacks.Cancel())
        return;
    RealCancel(acArgs);
}

UI_MESSAGE_RESULTS HookProcess(SleepWaitMenu* apMenu, UIMessage& aMessage)
{
    if (!s_ending && (aMessage.eType == UIMessage::kHide || aMessage.eType == UIMessage::kForceHide) &&
        s_callbacks.Cancel && s_callbacks.Cancel())
    {
        spdlog::info("Shared wait menu: native hide requested, active {}, driver {}", apMenu->active, s_driver);
        return UI_MESSAGE_RESULTS::kHandled;
    }
    // A guest only consumes a host-authorized step; before the readiness barrier no game ticks.
    if (s_shared && aMessage.eType == UIMessage::kUpdate && (!s_running || !s_driver))
        return UI_MESSAGE_RESULTS::kHandled;
    return RealProcess(apMenu, aMessage);
}

// WaitStep also simulates NPCs, physics and projectiles. Only the host fast-forwards the world.
void StepGuestPlayer(PlayerCharacter& aPlayer, uint32_t aRemainingSeconds) noexcept
{
    if (aRemainingSeconds >= aPlayer.sleepSeconds)
        return;
    const uint32_t cElapsed = aPlayer.sleepSeconds - aRemainingSeconds;
    const auto* cpTime = TimeData::Get();
    const float cDelta = cpTime && cpTime->TimeScale && cpTime->TimeScale->f > 0.f
                             ? static_cast<float>(cElapsed) / cpTime->TimeScale->f
                             : 0.f;
    POINTER_SKYRIMSE(ProcessLists*, s_processLists, 400315);
    using TReadClock = float(ProcessLists*);
    using TSetClock = int(ProcessLists*, float);
    using TUpdatePlayer = void(PlayerCharacter*, float);
    using TStop = uint64_t(PlayerCharacter*, bool);
    POINTER_SKYRIMSE(TReadClock, s_readClock, 41337);
    POINTER_SKYRIMSE(TSetClock, s_setClock, 41338);
    POINTER_SKYRIMSE(TUpdatePlayer, s_updatePlayer, 38785);
    POINTER_SKYRIMSE(TStop, s_stop, 40417);
    ProcessLists* pLists = *s_processLists.Get();
    aPlayer.InterruptCast(false);
    s_setClock(pLists, s_readClock(pLists) + cDelta);
    s_updatePlayer(&aPlayer, cDelta);
    aPlayer.sleepSeconds = aRemainingSeconds;
    if (!aRemainingSeconds)
        s_stop(&aPlayer, true);
}

uint64_t HookStep(PlayerCharacter* apPlayer)
{
    if (s_shared && s_running && !s_driver)
    {
        StepGuestPlayer(*apPlayer, s_nextRemainingSeconds);
        return 0;
    }
    const auto cResult = RealStep(apPlayer);
    if (s_shared && s_driver && s_running && s_callbacks.Stepped)
        s_callbacks.Stepped(apPlayer->sleepSeconds);
    return cResult;
}

static Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(52488, &RealOkay, HookOkay);
    HOOK_ID(52489, &RealCancel, HookCancel);
    HOOK_ID(52486, &RealProcess, HookProcess);
    HOOK_ID(40485, &RealStep, HookStep);
});
} // namespace

WaitBridge::~WaitBridge()
{
    s_callbacks = {};
}

void WaitBridge::SetCallbacks(Callbacks aCallbacks) noexcept
{
    s_callbacks = std::move(aCallbacks);
}

bool WaitBridge::Prepare(uint32_t aHours, bool aSleeping, bool aDriver) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || pPlayer->IsDead() || pPlayer->IsInCombat() || !UI::Get() || !UIMessageQueue::Get())
        return false;
    s_shared = true;
    s_ending = s_running = false;
    s_driver = aDriver;
    s_sleeping = aSleeping;
    s_hours = aHours;
    if (!Menu())
    {
        BSUIMessageData* pData = UIMessageQueue::CreateData();
        if (!pData)
            return false;
        pData->data.b = aSleeping;
        UIMessageQueue::Get()->AddMessage(kMenu, UIMessage::kShow, pData);
    }
    return true;
}

bool WaitBridge::IsReady() const noexcept
{
    const auto* cpMenu = Menu();
    GFxValue hours{};
    return cpMenu && cpMenu->uiMovie && cpMenu->PausesGame() &&
           cpMenu->uiMovie->GetVariable(&hours, "_root.SleepWaitMenu_mc.HoursSlider.value") &&
           (hours.type & GFxValue::kTypeMask) == GFxValue::kNumber;
}

void WaitBridge::Begin() noexcept
{
    auto* pMenu = Menu();
    if (!pMenu || !pMenu->uiMovie)
        return;
    pMenu->sleeping = s_sleeping;
    GFxValue hours{};
    hours.type = GFxValue::kNumber;
    hours.value.number = s_hours;
    GFxValue sleeping{};
    sleeping.type = GFxValue::kBoolean;
    sleeping.value.boolean = s_sleeping;
    pMenu->uiMovie->SetVariable("_root.SleepWaitMenu_mc.HoursSlider.value", hours, 0);
    pMenu->uiMovie->Invoke("_root.SleepWaitMenu_mc.sliderChange", nullptr, nullptr, 0);
    pMenu->uiMovie->Invoke("_root.SleepWaitMenu_mc.SetSleeping", nullptr, &sleeping, 1);
    GFxValue disabled{};
    disabled.type = GFxValue::kBoolean;
    disabled.value.boolean = true;
    pMenu->uiMovie->SetVariable("_root.SleepWaitMenu_mc.disableControls", disabled, 0);
    FxDelegateArgs args{};
    args.menu = pMenu;
    args.movie = pMenu->uiMovie;
    args.args = &hours;
    args.numArgs = 1;
    s_running = true;
    spdlog::info("Shared wait menu: starting {} hours, sleeping {}, driver {}", s_hours, s_sleeping, s_driver);
    // The native OK callback starts the player's wait/sleep, including its script events and autosave.
    RealOkay(args);
}

void WaitBridge::Step(uint32_t aRemainingSeconds) noexcept
{
    auto* pMenu = Menu();
    if (!pMenu || !s_running || s_driver)
        return;
    s_nextRemainingSeconds = aRemainingSeconds;
    pMenu->timer = 1.f;
    UIMessage update{};
    update.menu = kMenu;
    update.eType = UIMessage::kUpdate;
    RealProcess(pMenu, update);
}

void WaitBridge::End() noexcept
{
    const bool cOwned = s_shared || s_requested;
    s_shared = s_requested = s_running = false;
    s_ending = true;
    if (cOwned && UIMessageQueue::Get())
        UIMessageQueue::Get()->AddMessage(kMenu, UIMessage::kHide, nullptr);
}
