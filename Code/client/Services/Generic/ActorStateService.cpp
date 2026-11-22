#include <Services/ActorStateService.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/LocalComponent.h>
#include <Components/PlayerComponent.h>
#include <Diagnostics/Profile.h>
#include <Events/DisconnectedEvent.h>
#include <Events/FactionsChangeEvent.h>
#include <Events/PlayerRenamedEvent.h>
#include <Structs/Factions.h>
#include <Events/UpdateEvent.h>
#include <Games/Animation/ActorStateRules.h>
#include <Games/EngineNames.h>
#include <Services/CopyRecords.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>

ActorStateService::ActorStateService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge, CopyRecords& aRecords) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_records(aRecords)
{
    m_records.Register(
        RecordKind::kActorState,
        [this](uint32_t aId, const std::string&) {
            Applied applied = TryApply(aId);
            if (applied.Done)
                m_due.erase(aId);
            else
                m_due.insert(aId);
            return std::move(applied.Missing);
        },
        [this](uint32_t aFormId) -> std::optional<std::string> { return ToPartBytes(ReadPart(aFormId)); });
    m_records.Register(
        RecordKind::kFactions,
        [this](uint32_t aId, const std::string& acBytes) {
            ApplyFactions(aId, acBytes);
            return std::vector<uint32_t>{};
        },
        [this](uint32_t aFormId) -> std::optional<std::string> { return ToPartBytes(m_actorBridge.GetActorFactions(aFormId)); });
    m_records.Register(
        RecordKind::kName,
        [this](uint32_t aId, const std::string& acName) {
            ApplyName(aId, acName);
            return std::vector<uint32_t>{};
        },
        [this](uint32_t aFormId) -> std::optional<std::string> { return m_actorBridge.GetActorDisplayName(aFormId); });
    Listen<UpdateEvent, &ActorStateService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<FactionsChangeEvent, &ActorStateService::OnFactionsChange>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &ActorStateService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<&ActorStateService::OnLocalRemoved>(m_connections, m_world.on_destroy<LocalComponent>(), this);
}

ActorStateService::~ActorStateService() noexcept
{
    m_records.Register(RecordKind::kActorState, {});
    m_records.Register(RecordKind::kFactions, {});
    m_records.Register(RecordKind::kName, {});
}

ActorStatePart ActorStateService::ReadPart(uint32_t aFormId, const ActorStatePart* apLast) const noexcept
{
    const ActorStateFields cFields = m_actorBridge.GetActorStateFields(aFormId);
    ActorStatePart part;
    part.Tick = m_world.GetFrameTick();
    part.Flags1 = cFields.Flags1 & ActorStateRules::kPartBits1;
    part.Flags2 = cFields.Flags2 & ActorStateRules::kPartBits2;
    part.InKillMove = cFields.InKillMove;
    if (const auto cKiller = cFields.KillerFormId ? Utils::GetSessionIdByFormId(cFields.KillerFormId) : std::nullopt)
        part.Killer = *cKiller + 1;
    const WeaponState cWeapon = m_actorBridge.GetWeaponState(aFormId);
    const bool cDead = cFields.Dead || ActorStateRules::IsDeath(ActorStateRules::LifeOf(cFields.Flags1));
    part.HandsUp = ActorStateRules::SettledHands(cWeapon, cDead).value_or(apLast ? apLast->HandsUp : HandsUp(cWeapon));
    part.Noise = static_cast<uint32_t>(std::max(cFields.Noise, 0));
    return part;
}

void ActorStateService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PROFILE_ZONE("ActorStateService::OnUpdate");
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    for (const auto cEntity : view)
    {
        const uint32_t cFormId = view.get<FormIdComponent>(cEntity).Id;
        const uint32_t cId = view.get<LocalComponent>(cEntity).Id;
        if (!m_actorBridge.ActorExists(cFormId))
            continue;
        const auto cLast = m_sent.find(cId);
        const ActorStatePart cPart = ReadPart(cFormId, cLast != m_sent.end() ? &cLast->second : nullptr);
        if (cLast != m_sent.end() && cLast->second.SameState(cPart))
            continue;
        m_sent[cId] = cPart;
        m_records.Declare(cId, RecordKind::kActorState, ToPartBytes(cPart));
    }
    for (const auto cEntity : view)
        if (const uint32_t cFormId = view.get<FormIdComponent>(cEntity).Id; m_actorBridge.ActorExists(cFormId))
            if (DeclareIfChanged(view.get<LocalComponent>(cEntity).Id, RecordKind::kName, m_actorBridge.GetActorDisplayName(cFormId)) &&
                m_world.all_of<PlayerComponent>(cEntity) && !m_actorBridge.IsActorCopy(cFormId))
                m_world.GetDispatcher().trigger(PlayerRenamedEvent{});

    for (auto& [cId, since] : m_handsTried)
        since += acEvent.Delta;
    for (auto it = m_due.begin(); it != m_due.end();)
    {
        const uint32_t cId = *it;
        const Applied cApplied = TryApply(cId);
        // A killer not here yet: applied again when it appears (R5).
        for (const uint32_t cNamed : cApplied.Missing)
            m_records.WaitFor(cId, RecordKind::kActorState, cNamed);
        if (cApplied.Done)
            m_handsTried.erase(cId);
        it = cApplied.Done ? m_due.erase(it) : std::next(it);
    }
}

ActorStateService::Applied ActorStateService::TryApply(uint32_t aId) noexcept
{
    const RecordPart* cpRecord = m_records.Find(aId, RecordKind::kActorState);
    ActorStatePart part;
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aId);
    // Not a copy here: OnSessionId applies it when one is made.
    if (!cpRecord || !FromPartBytes(cpRecord->Bytes, part) || !cFormId || !m_actorBridge.IsActorCopy(cFormId))
        return {true, {}};
    const auto cPlayback = m_world.GetGraphStreams().StreamPlayback(cFormId);
    if (!m_actorBridge.ActorHas3D(cFormId) || !ActorStateRules::Due(cPlayback.has_value(), cPlayback.value_or(0), part.Tick))
        return {false, {}};

    const ActorStateFields cFields = m_actorBridge.GetActorStateFields(cFormId);
    m_actorBridge.SetCopyActorStateFlags(cFormId, ActorStateRules::FromPart1(part.Flags1, cFields.Flags1),
                                         ActorStateRules::FromPart2(part.Flags2, cFields.Flags2));
    // Its owner's noise, which the host's detection of it hears (stealth.md, "A player's noise").
    if (cFields.Noise != static_cast<int32_t>(part.Noise))
        m_actorBridge.SetCopyNoise(cFormId, static_cast<int32_t>(part.Noise));
    const WeaponState cWeapon = m_actorBridge.GetWeaponState(cFormId);
    Applied applied{cFields.Dead || ActorStateRules::HandsSettled(part.HandsUp, cWeapon), {}};
    const auto cTried = m_handsTried.find(aId);
    if (!cFields.Dead && ActorStateRules::PutsHandsRight(part.HandsUp, cWeapon, cPlayback.has_value()) &&
        (cTried == m_handsTried.end() || cTried->second >= ActorStateRules::kHandsRetry))
    {
        m_handsTried[aId] = 0.0;
        spdlog::debug("Copy {:X}: hands {} as its owner's (weapon state {})", cFormId, part.HandsUp ? "up" : "down", static_cast<int>(cWeapon));
        // On its owner's stream its graph is its owner's already and drops its own events: the result alone.
        if (cPlayback)
            m_actorBridge.ShowActorWeaponDrawn(cFormId, part.HandsUp);
        else
            m_actorBridge.SetActorWeaponDrawnEx(cFormId, part.HandsUp);
    }

    if (ActorStateRules::EntersKillMove(part.InKillMove, cFields.InKillMove, cFields.Dead))
    {
        const uint32_t cKiller = part.Killer ? Utils::GetFormIdBySessionId(part.Killer - 1) : 0;
        if (part.Killer && !cKiller)
            applied.Missing.push_back(part.Killer - 1);
        else
            m_actorBridge.CopyEnterKillMove(cFormId, cKiller);
    }
    else if (ActorStateRules::LeavesKillMove(part.InKillMove, cFields.InKillMove, cFields.InPair, cFields.Dead))
    {
        spdlog::debug("Copy {:X}: out of a kill move its owner left", cFormId);
        m_actorBridge.CopyLeaveKillMove(cFormId);
    }
    // In its pair still: its own KillMoveEnd ends it; looked at again in case that never comes.
    else if (!part.InKillMove && cFields.InKillMove && !cFields.Dead)
        applied.Done = false;
    return applied;
}

void ActorStateService::OnFactionsChange(const FactionsChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;
    const auto cId = Utils::GetSessionIdByFormId(acEvent.ActorId);
    if (!cId || m_actorBridge.IsActorCopy(acEvent.ActorId))
        return;
    DeclareIfChanged(*cId, RecordKind::kFactions, ToPartBytes(m_actorBridge.GetActorFactions(acEvent.ActorId)));
}

bool ActorStateService::DeclareIfChanged(uint32_t aId, RecordKind aKind, std::string aBytes) noexcept
{
    // Its last declared: the record holds it.
    if (const RecordPart* cpHeld = m_records.Find(aId, aKind); cpHeld && cpHeld->Bytes == aBytes)
        return false;
    m_records.Declare(aId, aKind, std::move(aBytes));
    return true;
}

void ActorStateService::ApplyName(uint32_t aId, const std::string& acName) noexcept
{
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aId);
    // An empty name (an unnamed base) leaves the copy's own (names.md).
    if (!cFormId || !m_actorBridge.IsActorCopy(cFormId) || acName.empty() || SameName(m_actorBridge.GetActorDisplayName(cFormId), acName))
        return;
    m_actorBridge.SetActorDisplayName(cFormId, acName);
}

void ActorStateService::ApplyFactions(uint32_t aId, const std::string& acBytes) noexcept
{
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aId);
    Factions factions;
    if (!cFormId || !m_actorBridge.IsActorCopy(cFormId) || !FromPartBytes(acBytes, factions))
        return;
    m_actorBridge.SetActorFactions(cFormId, factions);
}

void ActorStateService::OnLocalRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    // A later ownership term declares its state again.
    m_sent.erase(aRegistry.get<LocalComponent>(aEntity).Id);
}

void ActorStateService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_sent.clear();
    m_due.clear();
    m_handsTried.clear();
}
