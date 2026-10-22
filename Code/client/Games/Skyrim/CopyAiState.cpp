#include <HookSwitch.h>

#include <Actor.h>
#include <Forms/TESQuest.h>
#include <Games/ActorExtension.h>
#include <Games/CopyAiState.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

#include <intrin.h>

// The AI state a copy answers from its owner's where the engine asks an object, not a field
// (docs/design/copy-contract.md, "AI state"; the fields are written by IActorBridge::SetCopyAiFields).

using TGetCurrentScene = BGSScene*(Actor*);
static TGetCurrentScene* RealGetCurrentScene = nullptr;

// Actor::GetCurrentScene (37247, the Actor override of TESObjectREFR vfunc 0x4A): what IsInScene (21682) and
// Papyrus GetCurrentScene read.
static BGSScene* HookGetCurrentScene(Actor* apThis)
{
    if (apThis->GetExtension()->IsCopy())
    {
        const uint32_t cSceneId = apThis->GetExtension()->OwnerSceneId;
        return cSceneId ? Cast<BGSScene>(TESForm::GetById(cSceneId)) : nullptr;
    }
    return RealGetCurrentScene(apThis);
}

BGSScene* Actor::GetOwnCurrentScene() const noexcept
{
    return RealGetCurrentScene ? RealGetCurrentScene(const_cast<Actor*>(this)) : nullptr;
}

using TIsInCombat = bool(Actor*);
static TIsInCombat* RealIsInCombat = nullptr;
using TGetCombatState = uint32_t(Actor*);
static TGetCombatState* RealGetCombatState = nullptr;

// Who gets a copy's owner's combat state: the condition functions (IsInCombat 21576, GetCombatState 21740) and
// plugins (any caller outside the game's image, as Open Animation Replacer). The engine's own callers act on the
// combat controller after a yes, which a copy never has, often through a wrapper (EngagingInCombat 38563: weapon
// fire, spell aim): they get the copy's own answer (audit: docs/re/systems/actor-ai-and-copies.md, "Combat state
// and its readers"). Return addresses: the function's Address Library ID and the offset after its call.
static constexpr std::pair<uint32_t, uint32_t> cOwnerAnswerSites[] = {
    {21576, 0x38}, // GameFunc::native::IsInCombat
    {21740, 0x28}, // GameFunc::native::GetCombatState
};

// A caller outside the game's image: a plugin.
static bool IsOutsideGame(uintptr_t aAddress) noexcept
{
    static const auto s_image = []() {
        const auto cBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(cBase);
        const auto* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(cBase + pDos->e_lfanew);
        return std::pair{cBase, cBase + pNt->OptionalHeader.SizeOfImage};
    }();
    return aAddress < s_image.first || aAddress >= s_image.second;
}

static bool GetsOwnersCombatState(const void* apReturnAddress) noexcept
{
    static const auto s_sites = []() {
        std::vector<uintptr_t> sites;
        for (const auto& [cId, cOffset] : cOwnerAnswerSites)
        {
            VersionDbPtr<uint8_t> function(cId);
            sites.push_back(reinterpret_cast<uintptr_t>(function.Get()) + cOffset);
        }
        return sites;
    }();
    const auto cAddress = reinterpret_cast<uintptr_t>(apReturnAddress);
    return IsOutsideGame(cAddress) || std::ranges::find(s_sites, cAddress) != s_sites.end();
}

// Actor::IsInCombat (38562, vfunc 0xE3): a copy has no combat controller of its own.
static bool HookIsInCombat(Actor* apThis)
{
    if (apThis->GetExtension()->IsCopy() && GetsOwnersCombatState(_ReturnAddress()))
        return apThis->GetExtension()->OwnerCombatState != 0;
    return RealIsInCombat(apThis);
}

// Actor::GetCombatState (38556): 0 none, 1 fighting, 2 searching (its combat group's search state).
static uint32_t HookGetCombatState(Actor* apThis)
{
    if (apThis->GetExtension()->IsCopy() && GetsOwnersCombatState(_ReturnAddress()))
        return apThis->GetExtension()->OwnerCombatState;
    return RealGetCombatState(apThis);
}

static std::atomic<uint32_t> s_copiesFightingPlayer{0};

void CopyAiState::SetCopiesFightingPlayer(uint32_t aCount) noexcept
{
    s_copiesFightingPlayer.store(aCount, std::memory_order_relaxed);
}

using TCountCombatTargetingPlayer = void(void* apCombatManager, uint32_t aCount[2]);
static TCountCombatTargetingPlayer* RealCountCombatTargetingPlayer = nullptr;

// 41261, called by PlayerCharacter::UpdateInCombat (41238), which counts the player in combat when [1] is not 0:
// [1] adds the members of every combat group targeting the player. Copies have no groups: those fighting the player
// count as members.
static void HookCountCombatTargetingPlayer(void* apCombatManager, uint32_t aCount[2])
{
    RealCountCombatTargetingPlayer(apCombatManager, aCount);
    aCount[1] += s_copiesFightingPlayer.load(std::memory_order_relaxed);
}

uint8_t Actor::GetOwnCombatState() const noexcept
{
    return RealGetCombatState ? static_cast<uint8_t>(RealGetCombatState(const_cast<Actor*>(this))) : 0;
}

static Hooks::Initializer s_copyAiStateHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TGetCurrentScene, s_getCurrentScene, 37247);
        POINTER_SKYRIMSE(TIsInCombat, s_isInCombat, 38562);
        POINTER_SKYRIMSE(TGetCombatState, s_getCombatState, 38556);
        POINTER_SKYRIMSE(TCountCombatTargetingPlayer, s_countCombatTargetingPlayer, 41261);
        RealGetCurrentScene = s_getCurrentScene.Get();
        RealIsInCombat = s_isInCombat.Get();
        RealGetCombatState = s_getCombatState.Get();
        RealCountCombatTargetingPlayer = s_countCombatTargetingPlayer.Get();
        HOOK(&RealGetCurrentScene, HookGetCurrentScene);
        HOOK(&RealIsInCombat, HookIsInCombat);
        HOOK(&RealGetCombatState, HookGetCombatState);
        HOOK(&RealCountCombatTargetingPlayer, HookCountCombatTargetingPlayer);
    });
