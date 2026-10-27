#include <Bridges/CrimeBridge.h>

#include <Actor.h>
#include <Crime.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraOwnership.h>
#include <FormIds.h>
#include <Forms/TESFaction.h>
#include <Forms/TESObjectCELL.h>
#include <Games/ActorExtension.h>
#include <Games/Memory.h>
#include <Games/TES.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Papyrus/GuestScripts.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <SessionIds.h>
#include <VersionDb.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <mutex>

namespace
{
std::atomic<ICrimeBridge::Role> s_role{};
std::mutex s_mutex;
std::vector<CrimeRequest> s_actions;
std::vector<CrimeConfiscation> s_confiscations;
std::vector<CrimeItem> s_containerItems;
thread_local Actor* t_criminal{};
thread_local Actor* t_payer{};
uint32_t s_lockAttempt{};
std::atomic<uint32_t> s_caughtLock{};
bool s_reactionHooksAttempted{};

struct ScopedCriminal
{
    explicit ScopedCriminal(Actor* aActor) : Previous(std::exchange(t_criminal, aActor))
    {
    }
    ~ScopedCriminal()
    {
        t_criminal = Previous;
    }
    Actor* Previous;
};

bool OtherPlayer(Actor* aActor) noexcept
{
    return aActor && aActor->GetExtension()->IsOtherPlayer();
}

bool PlayerEnemy(TESFaction* aFaction) noexcept
{
    // TESFaction flags +0x58, kPlayerIsEnemy (24504/24505, AE 1.6.1170).
    return aFaction && (reinterpret_cast<const CrimeEngine::Faction*>(aFaction)->Flags & 8);
}

std::span<TESFaction* const> CrimeFactions() noexcept
{
    auto* data = ModManager::Get();
    if (!data)
        return {};
    // TESDataHandler's form arrays: +0x10 + form type (FACT=11) * 0x18.
    const auto& factions = reinterpret_cast<const CrimeEngine::DataHandler*>(data)->Factions;
    return {factions.data, factions.length};
}

using NativeGold = CrimeEngine::Gold;

CrimeEngine::GoldMap& Gold(PlayerCharacter* aPlayer) noexcept
{
    return reinterpret_cast<CrimeEngine::Player*>(aPlayer)->Bounty;
}

NativeGold* FindGold(PlayerCharacter* aPlayer, TESFaction* aFaction) noexcept
{
    auto& map = Gold(aPlayer);
    if (!map.Entries)
        return nullptr;
    for (uint32_t i = 0; i < map.Capacity; ++i)
        if (map.Entries[i].Next && map.Entries[i].Faction == aFaction)
            return &map.Entries[i].Value;
    return nullptr;
}

void Target(CrimeRequest& aRequest, TESObjectREFR* aRef) noexcept
{
    if (!aRef)
        return;
    if (auto id = Utils::GetSessionIdByFormId(aRef->formID))
        aRequest.TargetActor = *id;
    else if (auto id = ToSession(aRef->formID))
        aRequest.Target = *id;
}

void Queue(CrimeRequest aRequest) noexcept
{
    std::lock_guard lock(s_mutex);
    s_actions.push_back(std::move(aRequest));
}

void QueueItemCrime(CrimeAction aAction, TESObjectREFR* aTarget, TESForm* aItem, int32_t aCount,
                   bool aWarning = false)
{
    if (!aItem || aCount <= 0)
        return;
    if (auto item = ToSession(aItem->formID))
    {
        CrimeRequest request;
        request.Action = aAction;
        Target(request, aTarget);
        request.Item = *item;
        request.Count = aCount;
        request.AllowWarning = aWarning;
        Queue(std::move(request));
    }
}

using TSteal = void(Actor*, TESObjectREFR*, TESForm*, int32_t, int32_t, TESForm*, bool);
using TPickpocket = void(PlayerCharacter*, TESObjectREFR*, TESForm*, int32_t);
using TAssault = void(Actor*, Actor*, bool, uint32_t);
using TMurder = void(Actor*, Actor*);
using TTrespass = void(Actor*, TESObjectREFR*, TESForm*, uint32_t);
using TProcessKnowledge = void*(void*, Actor*, Actor*);
using TCrimeReaction = int32_t(Actor*, Actor*, bool);
using TCrimeGold = uint32_t(Actor*, TESFaction*);
using TCrimeAttack = bool(Actor*, Actor*, int32_t, uint8_t);
using TKnownCrime = bool(ProcessLists*, TESObjectREFR*, TESObjectREFR*, Actor*, uint32_t, int32_t, int32_t);
using TPayFine = void(PlayerCharacter*, TESFaction*, bool, bool);
using TPayGold = uintptr_t(TESObjectREFR*, TESObjectREFR*, uint32_t);
using TConfiscate = uintptr_t(ExtraContainerChanges::Data*, TESObjectREFR*, TESObjectREFR*, uint32_t, bool, bool);
using TLockOpen = uintptr_t(TESObjectREFR*);
using TLockCrime = bool(void*);
using TContainerTake = bool(void*, ExtraContainerChanges::Entry*, uint32_t, bool);
TSteal* RealSteal{};
TPickpocket* RealPickpocket{};
TAssault* RealAssault{};
TMurder* RealMurder{};
TMurder* RealWerewolf{};
TTrespass* RealTrespass{};
TProcessKnowledge* RealProcessKnowledge{};
TCrimeReaction* RealCrimeReaction{};
TCrimeGold* RealCrimeGold{};
TCrimeAttack* RealCrimeAttack{};
TKnownCrime* RealKnownCrime{};
TPayFine* RealPayFine{};
TPayGold* RealPayGold{};
TConfiscate* RealConfiscate{};
TLockOpen* RealLockOpen{};
TLockCrime* RealLockCrime{};
TContainerTake* RealContainerTake{};

bool HookContainerTake(void* aMenu, ExtraContainerChanges::Entry* aEntry, uint32_t aCount, bool aTaking)
{
    static VersionDbPtr<uint32_t> mode(405937);
    const bool observe = s_role == ICrimeBridge::Role::Guest && *mode.Get() == 1 && aEntry && aEntry->form;
    CrimeItem line;
    auto* value = &static_cast<CrimeEngine::ContainerMenu*>(aMenu)->StealValue;
    const auto before = *value;
    if (observe)
    {
        line.Item.BaseId = ToSession(aEntry->form->formID).value_or(GameId{});
        line.Taking = aTaking;
        if (aEntry->dataList)
            for (auto* extra : *aEntry->dataList)
                if (extra)
                {
                    if (line.Owners.empty())
                        TESObjectREFR::GetItemFromExtraData(line.Item, extra);
                    auto* ownership = static_cast<ExtraOwnership*>(extra->GetByType(ExtraDataType::Ownership));
                    auto* count = static_cast<ExtraCount*>(extra->GetByType(ExtraDataType::Count));
                    line.Owners.push_back({ownership && ownership->owner
                                               ? ToSession(ownership->owner->formID).value_or(GameId{})
                                               : GameId{},
                                           count ? static_cast<uint32_t>(count->count) : 1u});
                }
        line.Item.Count = static_cast<int32_t>(aCount);
    }
    const bool result = RealContainerTake(aMenu, aEntry, aCount, aTaking);
    if (observe)
    {
        std::lock_guard lock(s_mutex);
        if (!before)
            s_containerItems.clear();
        if (*value > before && line.Item.BaseId && line.Item.Count > 0)
            s_containerItems.push_back(std::move(line));
    }
    return result;
}

uintptr_t HookLockOpen(TESObjectREFR* aRef)
{
    if (!++s_lockAttempt)
        ++s_lockAttempt;
    return RealLockOpen(aRef);
}

bool HookLockCrime(void* aMenu)
{
    if (s_role != ICrimeBridge::Role::Guest)
        return RealLockCrime(aMenu);
    if (s_lockAttempt && s_caughtLock == s_lockAttempt)
    {
        static_cast<CrimeEngine::LockpickMenu*>(aMenu)->Reported = true;
        return true;
    }
    static VersionDbPtr<TESObjectREFR*> target(406271);
    if (auto* ref = *target.Get())
    {
        CrimeRequest request;
        request.Action = CrimeAction::Lockpick;
        Target(request, ref);
        request.Count = s_lockAttempt;
        Queue(std::move(request));
    }
    return false; // Host witnesses judge every attempt; no local crime is created.
}

void HookSteal(Actor* aActor, TESObjectREFR* aRef, TESForm* aItem, int32_t aCount, int32_t aTotal, TESForm* aOwner,
               bool aWarning)
{
    if (s_role == ICrimeBridge::Role::Guest && aActor == PlayerCharacter::Get())
    {
        if (!aItem && aTotal > 0)
        {
            CrimeRequest request;
            request.Action = CrimeAction::ContainerTheft;
            Target(request, aRef);
            request.AllowWarning = aWarning;
            {
                std::lock_guard lock(s_mutex);
                request.Items = std::exchange(s_containerItems, {});
            }
            if (!request.Items.empty())
                Queue(std::move(request));
            return;
        }
        QueueItemCrime(CrimeAction::Theft, aRef, aItem, aCount, aWarning);
        return;
    }
    // Activation replay on a guest copy is not a second theft: the guest sends its native alarm.
    if (s_role == ICrimeBridge::Role::Host && OtherPlayer(aActor))
        return;
    RealSteal(aActor, aRef, aItem, aCount, aTotal, aOwner, aWarning);
}

void HookPickpocket(PlayerCharacter* aPlayer, TESObjectREFR* aVictim, TESForm* aItem, int32_t aCount)
{
    if (s_role == ICrimeBridge::Role::Guest && aPlayer == PlayerCharacter::Get())
    {
        QueueItemCrime(CrimeAction::Pickpocket, aVictim, aItem, aCount);
        return;
    }
    RealPickpocket(aPlayer, aVictim, aItem, aCount);
}

void HookAssault(Actor* aVictim, Actor* aAttacker, bool aHostile, uint32_t aValue)
{
    if (s_role == ICrimeBridge::Role::Guest)
        return; // The host's damage replay already raises this alarm.
    ScopedCriminal scope(s_role == ICrimeBridge::Role::Host && OtherPlayer(aAttacker) ? aAttacker : t_criminal);
    RealAssault(aVictim, t_criminal ? PlayerCharacter::Get() : aAttacker, aHostile, aValue);
}

void HookMurder(Actor* aVictim, Actor* aAttacker)
{
    if (s_role == ICrimeBridge::Role::Guest)
        return;
    ScopedCriminal scope(s_role == ICrimeBridge::Role::Host && OtherPlayer(aAttacker) ? aAttacker : t_criminal);
    RealMurder(aVictim, t_criminal ? PlayerCharacter::Get() : aAttacker);
}

void HookWerewolf(Actor* aWitness, Actor* aActor)
{
    if (s_role == ICrimeBridge::Role::Guest)
        return;
    ScopedCriminal scope(s_role == ICrimeBridge::Role::Host && OtherPlayer(aActor) ? aActor : t_criminal);
    RealWerewolf(aWitness, t_criminal ? PlayerCharacter::Get() : aActor);
}

void HookTrespass(Actor* aActor, TESObjectREFR* aRef, TESForm* aOwner, uint32_t aCrime)
{
    if (s_role == ICrimeBridge::Role::Guest)
        return;
    ScopedCriminal scope(s_role == ICrimeBridge::Role::Host && OtherPlayer(aActor) ? aActor : t_criminal);
    RealTrespass(t_criminal ? PlayerCharacter::Get() : aActor, aRef, aOwner, aCrime);
}

void* HookProcessKnowledge(void* aProcess, Actor* aWitness, Actor* aTarget)
{
    // RequestDetectionLevel (37764), GetDetectionState (37758, used by HighestDetection)
    // and murder all read here. One scoped target adjustment covers all three paths.
    return RealProcessKnowledge(aProcess, aWitness,
                                t_criminal && aTarget == PlayerCharacter::Get() ? t_criminal : aTarget);
}

uint32_t HookCrimeGold(Actor* aActor, TESFaction* aFaction)
{
    // NPC bodies normally read the faction's NPC-base map, where a guest has no bounty.
    // Every player's actor exposes the same native player bounty, without writing fake NPC crimes.
    if (s_role != ICrimeBridge::Role::Offline && OtherPlayer(aActor) && PlayerCharacter::Get())
    {
        static VersionDbPtr<TCrimeGold> get(40641);
        return get(PlayerCharacter::Get(), aFaction);
    }
    return RealCrimeGold(aActor, aFaction);
}

bool HookKnownCrime(ProcessLists* aProcesses, TESObjectREFR* aVictim, TESObjectREFR* aCriminal, Actor* aWitness,
                    uint32_t aType, int32_t aUnknown, int32_t aDay)
{
    // The native record is the party's. Conditions such as GetCrime/GetCrimeKnown
    // querying a guest must find that same record, with the original victim/witness filters.
    if (s_role == ICrimeBridge::Role::Host && OtherPlayer(Cast<Actor>(aCriminal)))
        aCriminal = PlayerCharacter::Get();
    return RealKnownCrime(aProcesses, aVictim, aCriminal, aWitness, aType, aUnknown, aDay);
}

bool CriminalInterest(Actor* aWitness)
{
    if (!aWitness || OtherPlayer(aWitness))
        return false;
    using TFaction = TESFaction*(Actor*);
    using TCrimeList = GameList<void>*(ExtraDataList*);
    static VersionDbPtr<TFaction> faction(37380);
    static VersionDbPtr<TCrimeList> crimes(11726);
    const auto* known = crimes(&aWitness->extraData);
    return PlayerEnemy(faction(aWitness)) || (known && (known->entry.data || known->entry.next));
}

bool HookCrimeAttack(Actor* aWitness, Actor* aTarget, int32_t aAggression, uint8_t aFlags)
{
    if (s_role == ICrimeBridge::Role::Host && OtherPlayer(aTarget) && CriminalInterest(aWitness))
    {
        ScopedCriminal scope(aTarget);
        // Native player-only crime branches (including unaggressive victims and
        // guards) apply to every party body, using that body's detection/knowledge.
        return RealCrimeAttack(aWitness, PlayerCharacter::Get(), aAggression, aFlags);
    }
    return RealCrimeAttack(aWitness, aTarget, aAggression, aFlags);
}

int32_t HookCrimeReaction(Actor* aWitness, Actor* aTarget, bool aIgnoreCrime)
{
    const auto normal = RealCrimeReaction(aWitness, aTarget, aIgnoreCrime);
    if (s_role != ICrimeBridge::Role::Host || normal == 1 || !OtherPlayer(aTarget) || !CriminalInterest(aWitness))
        return normal;
    auto* player = PlayerCharacter::Get();
    if (!player || !player->currentProcess)
        return normal;
    // Copies skip knowledge caching, so adapt the underlying computation used
    // by both direct queries and cache refresh. This is also safe under the
    // knowledge-array read lock: it never calls ShouldAttack or inserts knowledge.
    return RealCrimeReaction(aWitness, player, aIgnoreCrime);
}

uintptr_t HookPayGold(TESObjectREFR* aPlayer, TESObjectREFR* aContainer, uint32_t aGold)
{
    // 37511 uses only TESObjectREFR's extra inventory, so a guest NPC is a valid payer.
    return RealPayGold(t_payer && aPlayer == PlayerCharacter::Get() ? t_payer : aPlayer, aContainer, aGold);
}

uintptr_t HookConfiscate(ExtraContainerChanges::Data* aItems, TESObjectREFR* aOwner, TESObjectREFR* aContainer,
                         uint32_t aReason, bool aStolen, bool aArg)
{
    if (t_payer && aOwner == PlayerCharacter::Get())
        return 0; // The chooser's own inventory retains stolen ownership; copies need not.
    return aItems ? RealConfiscate(aItems, aOwner, aContainer, aReason, aStolen, aArg) : 0;
}

void HookPayFine(PlayerCharacter* aPlayer, TESFaction* aFaction, bool aJail, bool aStolen)
{
    if (s_role == ICrimeBridge::Role::Guest)
    {
        if (aFaction)
            if (auto faction = ToSession(aFaction->formID))
            {
                CrimeRequest request;
                request.Action = CrimeAction::PayFine;
                request.Target = *faction;
                request.GoToJail = aJail;
                request.RemoveStolenItems = aStolen;
                Queue(std::move(request));
            }
        return;
    }
    Actor* payer =
        s_role == ICrimeBridge::Role::Host ? Cast<Actor>(TESForm::GetById(GuestScripts::NativeGuest())) : nullptr;
    Actor* previous = std::exchange(t_payer, OtherPlayer(payer) ? payer : nullptr);
    RealPayFine(aPlayer, aFaction, aJail, aStolen);
    if (s_role == ICrimeBridge::Role::Host)
    {
        // PayFine ends the real player's alarm. Resolve the same alarm/combat on
        // every guest body too; their fights are not stored on the host's body.
        using TStop = void(ProcessLists*, Actor*, bool);
        using TReset = void(Actor*, bool);
        static VersionDbPtr<TStop> stop(41340);
        static VersionDbPtr<TReset> reset(37668);
        reset(PlayerCharacter::Get(), true);
        if (auto* processes = ProcessLists::Get())
            for (auto handle : processes->highActorHandleArray)
                if (auto* actor = Cast<Actor>(TESObjectREFR::GetByHandle(handle)); OtherPlayer(actor))
                {
                    stop(processes, actor, false);
                    reset(actor, true);
                }
    }
    if (t_payer && aFaction && aStolen)
        if (auto faction = ToSession(aFaction->formID))
        {
            std::lock_guard lock(s_mutex);
            s_confiscations.push_back({t_payer->formID, *faction});
        }
    t_payer = previous;
}

void UpdateGuestTrespass(Actor* aActor)
{
    if (s_role != ICrimeBridge::Role::Host || !OtherPlayer(aActor))
        return;
    // The NPC-safe part of 37578. Never run PlayerCharacter's 42676 on an NPC.
    using TCheck = bool(TESObjectCELL*, Actor*);
    using TOwner = TESForm*(TESObjectCELL*);
    using TExtra = void*(ExtraDataList*);
    using TCreate = void*(void*, Actor*, TESForm*, uint32_t);
    using TAdd = void(ExtraDataList*, void*);
    static VersionDbPtr<TCheck> check(18909);
    static VersionDbPtr<TOwner> owner(18905);
    static VersionDbPtr<TExtra> extra(11723), clear(11942);
    static VersionDbPtr<TCreate> create(42678);
    static VersionDbPtr<TAdd> add(11635);
    static VersionDbPtr<uint32_t> warningTime(371115);
    TESObjectCELL* cell = aActor->GetParentCellEx();
    // Property permissions belong to the party's player. A copy's created NPC base
    // is not base 7, so checking it directly mislabels the party's own home.
    if (cell && check(cell, PlayerCharacter::Get()))
    {
        aActor->flags2 |= 0x1000;
        if (!extra(&aActor->extraData))
            if (void* memory = Memory::Allocate(256))
                add(&aActor->extraData, create(memory, aActor, owner(cell), *warningTime.Get()));
    }
    else
    {
        aActor->flags2 &= ~0x1000u;
        clear(&aActor->extraData);
    }
}

static Hooks::Initializer s_hooks([] {
    const HookGate gate(__FILE__);
    HOOK_ID(37422, &RealSteal, HookSteal);
    HOOK_ID(37423, &RealPickpocket, HookPickpocket);
    HOOK_ID(37425, &RealAssault, HookAssault);
    HOOK_ID(37379, &RealCrimeGold, HookCrimeGold);
    HOOK_ID(41354, &RealKnownCrime, HookKnownCrime);
    HOOK_ID(37426, &RealMurder, HookMurder);
    HOOK_ID(37428, &RealWerewolf, HookWerewolf);
    HOOK_ID(37427, &RealTrespass, HookTrespass);
    HOOK_ID(39515, &RealProcessKnowledge, HookProcessKnowledge);
    HOOK_ID(40659, &RealPayFine, HookPayFine);
    HOOK_ID(37511, &RealPayGold, HookPayGold);
    HOOK_ID(16120, &RealConfiscate, HookConfiscate);
    HOOK_ID(51959, &RealLockOpen, HookLockOpen);
    HOOK_ID(51981, &RealLockCrime, HookLockCrime);
    HOOK_ID(51141, &RealContainerTake, HookContainerTake);
});
} // namespace

CrimeBridge::~CrimeBridge()
{
    SetRole(Role::Offline);
}

void CrimeBridge::UpdatePlayers(std::span<const uint32_t> aActorFormIds) noexcept
{
    const auto state = ReadGold();
    const bool changed = state != m_reactionState || !std::ranges::equal(aActorFormIds, m_players);
    using TReset = void(Actor*, bool);
    static VersionDbPtr<TReset> reset(37668);
    auto* player = PlayerCharacter::Get();
    if (!player)
        return;
    for (auto id : aActorFormIds)
        if (auto* actor = Cast<Actor>(TESForm::GetById(id)); OtherPlayer(actor))
        {
            // The shared attack-on-sight flag is read on NPC targets too (37534).
            const bool attackChanged = ((actor->flags2 ^ player->flags2) & 0x8000u) != 0;
            if (attackChanged)
                actor->flags2 = (actor->flags2 & ~0x8000u) | (player->flags2 & 0x8000u);
            if (changed || attackChanged)
                reset(actor, true); // all observers discard cached crime reactions, including after a pardon
            UpdateGuestTrespass(actor); // copies skip the native UpdateAI that normally maintains this
        }
    m_reactionState = state;
    m_players.assign(aActorFormIds.begin(), aActorFormIds.end());
}

void CrimeBridge::SetRole(Role aRole) noexcept
{
    if (aRole != Role::Offline && !s_reactionHooksAttempted)
    {
        // Actor.cpp already owns 37534/37667 for friendly hits. Chain beneath
        // those hooks after startup so their entries and early returns survive.
        s_reactionHooksAttempted = true;
        const HookGate gate(__FILE__);
        Hooks::Resolve<37534>(&RealCrimeAttack);
        const bool installed = Hooks::FunctionHookManager::GetInstance().AddChained(Hooks::FunctionHook(
            reinterpret_cast<void**>(&RealCrimeAttack), reinterpret_cast<void*>(HookCrimeAttack), "HookCrimeAttack"));
        if (!installed)
            spdlog::error("Party crime: could not install the guest crime attack adapter");
        Hooks::Resolve<37667>(&RealCrimeReaction);
        if (!Hooks::FunctionHookManager::GetInstance().AddChained(Hooks::FunctionHook(
                reinterpret_cast<void**>(&RealCrimeReaction), reinterpret_cast<void*>(HookCrimeReaction), "HookCrimeReaction")))
            spdlog::error("Party crime: could not install the guest crime reaction adapter");
    }
    if (s_role.exchange(aRole) != aRole)
    {
        std::lock_guard lock(s_mutex);
        s_actions.clear();
        s_confiscations.clear();
        s_containerItems.clear();
        s_caughtLock = 0;
        m_reactionState.clear();
        m_players.clear();
    }
}

std::vector<CrimeRequest> CrimeBridge::TakeActions() noexcept
{
    std::lock_guard lock(s_mutex);
    return std::exchange(s_actions, {});
}

std::vector<CrimeGold> CrimeBridge::ReadGold() const noexcept
{
    std::vector<CrimeGold> rows;
    if (auto* player = PlayerCharacter::Get())
    {
        auto& map = Gold(player);
        if (map.Entries)
            for (uint32_t i = 0; i < map.Capacity; ++i)
            {
                const auto& entry = map.Entries[i];
                // Native clears retain an empty map entry. Omit it so an old guest
                // entry does not make an empty host snapshot look different every frame.
                if (entry.Next && entry.Faction &&
                    (entry.Value.Violent || entry.Value.NonViolent || entry.Value.NonViolentInfamy ||
                     entry.Value.ViolentInfamy || PlayerEnemy(entry.Faction)))
                    if (auto faction = ToSession(entry.Faction->formID))
                        rows.push_back({*faction, entry.Value.Violent, entry.Value.NonViolent,
                                        entry.Value.NonViolentInfamy, entry.Value.ViolentInfamy, PlayerEnemy(entry.Faction)});
            }
        // Scripted faction hostility can exist without any gold-map entry.
        for (auto* faction : CrimeFactions())
            if (PlayerEnemy(faction) && !FindGold(player, faction))
                if (auto id = ToSession(faction->formID))
                    rows.push_back({*id, 0, 0, 0, 0, true});
    }
    std::ranges::sort(rows, {}, [](const CrimeGold& aRow) { return ToGame(aRow.Faction); });
    return rows;
}

void CrimeBridge::ApplyGold(const std::vector<CrimeGold>& aRows) noexcept
{
    auto* player = PlayerCharacter::Get();
    if (!player)
        return;
    using TSet = void(PlayerCharacter*, TESFaction*, bool, uint32_t);
    using TEnemy = void(TESFaction*, bool);
    static VersionDbPtr<TEnemy> setEnemy(24505);
    auto set = reinterpret_cast<TSet**>(*reinterpret_cast<uintptr_t**>(player))[0xB5];
    const auto previous = ReadGold();
    for (const auto& row : previous)
        if (std::ranges::none_of(aRows, [&](const CrimeGold& aRow) { return aRow.Faction == row.Faction; }))
        {
            auto* faction = Cast<TESFaction>(TESForm::GetById(ToGame(row.Faction)));
            if (row.Violent)
                set(player, faction, true, 0);
            if (row.NonViolent)
                set(player, faction, false, 0);
            if (auto* value = FindGold(player, faction))
                value->NonViolentInfamy = value->ViolentInfamy = 0;
            if (row.PlayerEnemy)
                setEnemy(faction, false);
        }
    for (const auto& row : aRows)
        if (auto* faction = Cast<TESFaction>(TESForm::GetById(ToGame(row.Faction))))
        {
            auto it = FindGold(player, faction);
            if (!it || it->Violent != row.Violent)
                set(player, faction, true, row.Violent);
            it = FindGold(player, faction);
            if (!it || it->NonViolent != row.NonViolent)
                set(player, faction, false, row.NonViolent);
            it = FindGold(player, faction);
            if (it)
            {
                it->NonViolentInfamy = row.NonViolentInfamy;
                it->ViolentInfamy = row.ViolentInfamy;
            }
            if (PlayerEnemy(faction) != row.PlayerEnemy)
                setEnemy(faction, row.PlayerEnemy);
        }
}

bool CrimeBridge::Judge(uint32_t aActorFormId, const CrimeRequest& aRequest) noexcept
{
    auto* actor = Cast<Actor>(TESForm::GetById(aActorFormId));
    auto* player = PlayerCharacter::Get();
    if (!player || !OtherPlayer(actor) || !actor->currentProcess)
        return false;
    if (aRequest.Action == CrimeAction::PayFine)
    {
        auto* faction = Cast<TESFaction>(TESForm::GetById(ToGame(aRequest.Target)));
        auto* gold = TESForm::GetById(0xF);
        using TGet = uint32_t(PlayerCharacter*, TESFaction*);
        static VersionDbPtr<TGet> get(40641);
        if (!faction || !gold || actor->GetItemCountInInventory(gold) < get(player, faction))
            return false;
        GuestScripts::ScopedCause cause(aActorFormId);
        HookPayFine(player, faction, aRequest.GoToJail, aRequest.RemoveStolenItems);
        return false;
    }
    auto* target = Cast<TESObjectREFR>(TESForm::GetById(
        aRequest.TargetActor ? Utils::GetFormIdBySessionId(aRequest.TargetActor) : ToGame(aRequest.Target)));
    auto* item = TESForm::GetById(ToGame(aRequest.Item));
    if (!target || target->GetParentCellEx() != actor->GetParentCellEx())
        return false;
    // Recheck permission in the host's world; the guest's ownership information can be stale.
    using TOwns = bool(TESObjectREFR*, Actor*, bool, bool);
    static VersionDbPtr<TOwns> owns(20210);
    // An unowned actor is not permission to pick their pockets. The caught-pickpocket
    // alarm applies its own victim, forgiveness and crime-faction rules.
    if (aRequest.Action != CrimeAction::Pickpocket && owns(target, player, true, false))
        return false;
    ScopedCriminal scope(actor);
    GuestScripts::ScopedCause cause(aActorFormId);
    if (aRequest.Action == CrimeAction::ContainerTheft)
    {
        using TValue = uint32_t(TESObjectREFR*, ExtraContainerChanges::Entry*, int32_t, bool);
        using TSetOwner = void(ExtraDataList*, TESForm*);
        using TSetCount = void(ExtraDataList*, uint16_t);
        using TDestroy = void(ExtraDataList*, bool);
        static VersionDbPtr<TValue> value(16045);
        static VersionDbPtr<TSetOwner> setOwner(11609);
        static VersionDbPtr<TSetCount> setCount(11617);
        static VersionDbPtr<TDestroy> destroy(11573);
        uint64_t total{};
        for (const auto& line : aRequest.Items)
        {
            auto* form = TESForm::GetById(ToGame(line.Item.BaseId));
            if (!form || line.Item.Count <= 0)
                return false;
            std::vector<GameList<ExtraDataList>> nodes(line.Owners.size());
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                auto* extra = i == 0 ? TESObjectREFR::GetExtraDataFromItem(line.Item) : nullptr;
                if (!extra)
                    extra = ExtraDataList::New();
                setOwner(extra, TESForm::GetById(ToGame(line.Owners[i].Owner)));
                setCount(extra, static_cast<uint16_t>(line.Owners[i].Count));
                nodes[i].entry = {extra, i + 1 < nodes.size() ? &nodes[i + 1].entry : nullptr};
            }
            ExtraContainerChanges::Entry entry{form, nodes.empty() ? nullptr : nodes.data(), line.Item.Count, {}};
            total += std::max(1u, value(target, &entry, line.Item.Count, line.Taking));
            for (auto& node : nodes)
            {
                destroy(node.entry.data, true);
                Memory::Free(node.entry.data);
            }
            if (total > 0x7FFFFFFF)
                return false;
        }
        if (total)
            RealSteal(player, target, nullptr, 0, static_cast<int32_t>(total), target->GetOwner(),
                      aRequest.AllowWarning);
        return false;
    }
    if (!aRequest.Count)
        return false;
    if (aRequest.Action == CrimeAction::Lockpick)
    {
        // 51981 reads only its menu's +0x10D reported flag; all other inputs are the target
        // and native player/crime state. Keep its native detection, perks and jail-escape rules.
        CrimeEngine::LockpickMenu state;
        static VersionDbPtr<TESObjectREFR*> lockTarget(406271);
        auto* previous = std::exchange(*lockTarget.Get(), target);
        const bool reported = RealLockCrime(&state);
        *lockTarget.Get() = previous;
        return reported;
    }
    if (!item || aRequest.Count > 0x7FFFFFFFu)
        return false;
    spdlog::info("Party crime: actor {:X}, action {}, target {:X}, count {}", aActorFormId,
                 static_cast<uint32_t>(aRequest.Action), target->formID, aRequest.Count);
    if (aRequest.Action == CrimeAction::Theft)
        RealSteal(player, target, item, aRequest.Count, 0, target->GetOwner(), aRequest.AllowWarning);
    else if (aRequest.Action == CrimeAction::Pickpocket && Cast<Actor>(target))
        RealPickpocket(player, target, item, aRequest.Count);
    return false;
}

void CrimeBridge::LockCaught(uint32_t aAttempt) noexcept
{
    s_caughtLock = aAttempt;
}

std::vector<CrimeConfiscation> CrimeBridge::TakeConfiscations() noexcept
{
    std::lock_guard lock(s_mutex);
    return std::exchange(s_confiscations, {});
}

void CrimeBridge::Confiscate(const GameId& aFaction) noexcept
{
    auto* player = PlayerCharacter::Get();
    auto* faction = Cast<TESFaction>(TESForm::GetById(ToGame(aFaction)));
    if (!player || !faction)
        return;
    // PayFine 40659's faction stolen-goods chest, read from the same native field.
    auto* chest = reinterpret_cast<CrimeEngine::Faction*>(faction)->StolenContainer;
    if (chest)
        RealConfiscate(player->GetContainerChanges(), player, chest, 0, true, false);
}
