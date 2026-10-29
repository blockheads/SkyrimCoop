#include <CreationStart.h>

#include <EquipManager.h>
#include <FormIds.h>
#include <Forms/TESObjectCELL.h>
#include <Games/TES.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Interface/UI.h>
#include <MainLoopTasks.h>
#include <Papyrus/GlobalCall.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <SaveLoadManager.h>
#include <VersionDb.h>

#include <atomic>
#include <memory>

namespace CreationStart
{
namespace
{
using Phase = IJoinBridge::Creation;
struct State
{
    std::atomic<Phase> Current{Phase::kIdle};
    std::atomic<bool> Queued{false};
    std::atomic<bool> Started{false};
    bool RequestedMenu{};
    std::atomic<bool> SawMenu{false};
    std::atomic<bool> Named{false};
    std::atomic<bool> SpellsAsked{false};
    std::atomic<bool> SpellsGiven{false};
};
const auto s_state = std::make_shared<State>();

TESObjectCELL* Room() noexcept
{
    const auto* pMods = ModManager::Get();
    const auto* pMod = pMods ? pMods->GetByName("SkyrimCoopStart.esp") : nullptr;
    return pMod && pMod->IsLoaded() ? Cast<TESObjectCELL>(TESForm::GetById(pMod->GetFormId(0x800))) : nullptr;
}

using TChangeName = void(void*, const char*);
TChangeName* RealChangeName = nullptr;

// Native naming confirmation, not simply a menu disappearing. Unrelated renames pass through unchanged.
void HookChangeName(void* apMenu, const char* acpName)
{
    RealChangeName(apMenu, acpName);
    if (s_state->Current == Phase::kCustomizing && s_state->SawMenu && acpName && *acpName)
        s_state->Named = true;
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(52415, &RealChangeName, HookChangeName);
});

// IDA 1.6.1170: 55576 reads only the three booleans, writing PlayerCharacter::byCharGenFlag (0xB0A).
void InChargen(bool aValue) noexcept
{
    using TSetInChargen = void(void*, uint32_t, void*, bool, bool, bool);
    POINTER_SKYRIMSE(TSetInChargen, s_setInChargen, 55576);
    s_setInChargen(nullptr, 0, nullptr, aValue, aValue, false);
}

void StartingKit(PlayerCharacter& aPlayer) noexcept
{
    // COC's default player carries development equipment. Give a new adventurer only prisoner clothing.
    Inventory kit;
    for (const uint32_t cId : {0x0003C9FEu, 0x0003CA00u})
        if (const auto cGameId = ToSession(cId))
            kit.Entries.push_back({.BaseId = *cGameId, .Count = 1, .ExtraWorn = true});
    aPlayer.SetActorInventory(kit);
}

// The intro quest's script gives a new character its race's learned spells after naming (MQ101QuestScript
// AddRaceSpells, Skyrim.esm MQ101): the game's own call, as patched by mods, on the quest's bound script.
constexpr uint32_t cIntroQuest = 0x3372B;

void Finish(const std::shared_ptr<State>& apState) noexcept
{
    if (apState->SpellsAsked.exchange(true))
        return;
    const LocalCall cCall{.Class = "MQ101QuestScript", .Function = "AddRaceSpells", .Self = LocalValue{LocalForm{cIntroQuest}}};
    const bool cQueued = GlobalCall::QueueCall(cCall, [apState](LocalValue) { apState->SpellsGiven = true; });
    if (!cQueued)
    {
        spdlog::warn("CreationStart: the intro quest's race spells could not be asked for");
        apState->SpellsGiven = true;
    }
}

void Advance(const std::shared_ptr<State>& apState) noexcept
{
    const auto cPhase = apState->Current.load();
    auto* pPlayer = PlayerCharacter::Get();
    auto* pUI = UI::Get();
    if (!pUI || !pPlayer)
        return;
    const bool cMenu = pUI->GetMenuOpen("RaceSex Menu");
    if (cPhase == Phase::kStarting && !apState->Started.exchange(true))
    {
        auto* pCell = Room();
        if (!pUI->GetMenuOpen("Main Menu") || !pCell)
        {
            apState->Current = Phase::kFailed;
            return;
        }
        // CommonLibSSE-NG and IDA agree: 40437 takes (player, editorID or nullptr, cell or nullptr).
        // It initializes a fresh world from the main menu without starting the intro quest (52118).
        using TCenterOnCell = bool(PlayerCharacter*, const char*, TESObjectCELL*);
        POINTER_SKYRIMSE(TCenterOnCell, s_centerOnCell, 40437);
        POINTER_SKYRIMSE(void(BGSSaveLoadManager*), s_generateCharacterId, 35757);
        s_generateCharacterId(BGSSaveLoadManager::Get());
        InChargen(true);
        if (!s_centerOnCell(pPlayer, nullptr, pCell))
        {
            InChargen(false);
            apState->Current = Phase::kFailed;
        }
        return;
    }
    if (cPhase == Phase::kStarting && !pUI->GetMenuOpen("Main Menu") && !pUI->GetMenuOpen("Loading Menu") &&
        pPlayer->parentCell == Room() && pPlayer->GetNiNode() && !apState->RequestedMenu)
    {
        StartingKit(*pPlayer);
        apState->RequestedMenu = true;
        POINTER_SKYRIMSE(void(), s_showRaceMenu, 55580);
        s_showRaceMenu();
    }
    else if (cPhase == Phase::kStarting && apState->RequestedMenu && cMenu)
    {
        apState->SawMenu = true;
        apState->Current = Phase::kCustomizing;
    }
    else if (cPhase == Phase::kCustomizing)
    {
        if (cMenu)
            apState->SawMenu = true;
        if (apState->SawMenu && !cMenu && !pUI->GetMenuOpen("MessageBoxMenu") && !pUI->GetMenuOpen("TextEntryMenu"))
        {
            if (!apState->Named)
            {
                InChargen(false);
                apState->Current = Phase::kFailed;
                return;
            }
            apState->Current = Phase::kFinalizing;
        }
    }
    else if (cPhase == Phase::kFinalizing)
    {
        Finish(apState);
        if (!apState->SpellsGiven)
            return;
        InChargen(false);
        spdlog::info("CreationStart: the named character is ready");
        apState->Current = Phase::kComplete;
    }
}
} // namespace

bool Begin() noexcept
{
    if (!Room() || !RealChangeName)
        return false;
    Phase idle = Phase::kIdle;
    if (!s_state->Current.compare_exchange_strong(idle, Phase::kStarting))
        return false;
    s_state->Started = false;
    s_state->RequestedMenu = s_state->SawMenu = s_state->Named = s_state->SpellsAsked = s_state->SpellsGiven = false;
    return true;
}

void Cancel() noexcept
{
    // Once startup has entered the engine, finish normally; JoinService forgets the automatic join.
    if (!s_state->Started)
    {
        Phase starting = Phase::kStarting;
        s_state->Current.compare_exchange_strong(starting, Phase::kIdle);
    }
}

Phase Poll() noexcept
{
    const auto cPhase = s_state->Current.load();
    if (cPhase == Phase::kIdle)
        return cPhase;
    if (cPhase == Phase::kComplete || cPhase == Phase::kFailed)
    {
        s_state->Current = Phase::kIdle;
        return cPhase;
    }
    if (!s_state->Queued.exchange(true))
        MainLoopTasks::Queue([pState = s_state] {
            Advance(pState);
            pState->Queued = false;
        });
    return cPhase;
}
} // namespace CreationStart
