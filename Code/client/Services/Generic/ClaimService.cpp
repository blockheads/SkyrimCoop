#include <Services/ClaimService.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/LocalComponent.h>
#include <Components/RemoteComponent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Games/Animation/GraphStream.h>
#include <Games/Animation/ReplayState.h>
#include <Messages/Relayed.h>
#include <Services/CharacterService.h>
#include <Services/HostService.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>

ClaimService::ClaimService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
{
    Listen<NotifyActorClaim, &ClaimService::OnClaim>(m_connections, aDispatcher, this);
    Listen<NotifyActorGiveBack, &ClaimService::OnGiveBack>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &ClaimService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &ClaimService::OnDisconnected>(m_connections, aDispatcher, this);
}

entt::entity ClaimService::EntityOf(uint32_t aSessionId) const noexcept
{
    for (const auto cEntity : m_world.view<FormIdComponent>())
        if (Utils::GetSessionId(cEntity) == aSessionId)
            return cEntity;
    return entt::null;
}

bool ClaimService::Holds(uint32_t aSessionId) const noexcept
{
    return m_held.contains(aSessionId);
}

void ClaimService::OnClaim(const NotifyActorClaim& acMessage) noexcept
{
    // Given back and not taken yet: taken (and killed) first, as its last holder left it.
    TakeNow(acMessage.ActorId);
    const entt::entity cEntity = EntityOf(acMessage.ActorId);
    const entt::entity cFor = EntityOf(acMessage.ForId);
    const bool cOurs = cFor != entt::null && m_world.all_of<LocalComponent>(cFor);
    // Passed on to another game (a passenger drives on): ours to give back no longer.
    if (!cOurs)
    {
        m_held.erase(acMessage.ActorId);
        std::erase_if(m_giveBacks, [&](const Pending& acPending) { return acPending.SessionId == acMessage.ActorId; });
    }
    if (cEntity == entt::null)
        return;
    const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
    auto& characters = m_world.GetCharacterService();
    if (!cOurs)
    {
        if (m_world.all_of<LocalComponent>(cEntity))
            characters.GiveUpOwnership(cEntity, acMessage.ActorId, cFormId);
        return;
    }
    m_held.insert(acMessage.ActorId);
    if (!m_world.all_of<RemoteComponent>(cEntity) || characters.TakeOwnership(cFormId, acMessage.ActorId, cEntity, acMessage.DeathOnGiveBack))
        return;
    // Not takeable here (a player's summon): back at once.
    spdlog::warn("Claim of {:X} for {:X}: this game cannot take it, given back", cFormId, acMessage.ForId);
    GiveBack(acMessage.ActorId, false, true);
}

void ClaimService::GiveBack(uint32_t aSessionId, bool aKilled, bool aNow, std::function<void()> aBefore) noexcept
{
    const entt::entity cEntity = EntityOf(aSessionId);
    const uint32_t cFormId = cEntity != entt::null && m_world.all_of<LocalComponent>(cEntity) ? m_world.get<FormIdComponent>(cEntity).Id : 0;
    const auto cUpdates = cFormId ? m_world.GetGraphStreams().Updates(cFormId) : std::nullopt;
    m_giveBacks.push_back({aSessionId, cFormId, cUpdates ? cUpdates->Count : 0, aKilled, aNow, std::move(aBefore)});
    RunGiveBacks();
}

void ClaimService::RunGiveBacks() noexcept
{
    std::vector<Pending> due;
    std::erase_if(m_giveBacks, [&](Pending& aPending) {
        const auto cUpdates = aPending.FormId ? m_world.GetGraphStreams().Updates(aPending.FormId) : std::nullopt;
        if (!aPending.Now && cUpdates && cUpdates->Count <= aPending.Since)
            return false;
        due.push_back(std::move(aPending));
        return true;
    });
    for (Pending& pending : due)
    {
        const auto cUpdates = pending.FormId ? m_world.GetGraphStreams().Updates(pending.FormId) : std::nullopt;
        ActorGiveBackRequest request;
        request.ActorId = pending.SessionId;
        request.Killed = pending.Killed;
        // Giving it up drops its unsent ops: they go first, the give-back's tick their last update (copies take it then).
        request.Tick = cUpdates ? cUpdates->LastTick : 0;
        if (pending.FormId)
            request.Whereabouts = m_world.GetCharacterService().ReadWhereabouts(pending.FormId);
        m_world.GetCharacterService().SendGraphStream();
        if (pending.Before)
            pending.Before();
        m_transport.Send(request);
        m_held.erase(pending.SessionId);
    }
}

void ClaimService::OnGiveBack(const NotifyActorGiveBack& acMessage) noexcept
{
    m_held.erase(acMessage.ActorId);
    std::erase_if(m_giveBacks, [&](const Pending& acPending) { return acPending.SessionId == acMessage.ActorId; });
    const entt::entity cEntity = EntityOf(acMessage.ActorId);
    if (cEntity == entt::null)
        return;
    const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
    const bool cLocal = m_world.all_of<LocalComponent>(cEntity);
    // The host's game takes it once its copy has played the holder's last ops, placed where the holder left it.
    if (m_world.GetHostService().IsHosting())
    {
        if (!cLocal)
            m_takeOvers.push_back({acMessage.ActorId, acMessage.Tick, acMessage.Whereabouts, acMessage.Killed ? acMessage.ForId : 0,
                                   m_world.GetTick() + ReplayState::cPoseHoldMs});
        return;
    }
    if (cLocal)
        m_world.GetCharacterService().GiveUpOwnership(cEntity, acMessage.ActorId, cFormId);
    else if (acMessage.Whereabouts.Where == ActorWhereabouts::State::kPlaced)
        m_world.GetCharacterService().FollowOwner(cEntity, cFormId, acMessage.Whereabouts, false);
}

bool ClaimService::Take(const TakeOver& acTakeOver, bool aNow) noexcept
{
    const entt::entity cEntity = EntityOf(acTakeOver.SessionId);
    if (cEntity == entt::null || !m_world.all_of<RemoteComponent>(cEntity))
        return true; // gone, or ours already
    const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
    if (!aNow && acTakeOver.KillerId && !m_actorBridge.ActorKillCounted(cFormId) && m_world.GetTick() < acTakeOver.Until)
        return false;
    const auto cDone = m_world.GetGraphStreams().StreamDoneThrough(cFormId);
    if (!aNow && !ReplayState::TakesOverMount(acTakeOver.Tick, cDone.has_value(), cDone.value_or(0)))
        return false;
    auto& characters = m_world.GetCharacterService();
    if (acTakeOver.Whereabouts.Where == ActorWhereabouts::State::kPlaced)
        characters.FollowOwner(cEntity, cFormId, acTakeOver.Whereabouts, false);
    if (characters.TakeOwnership(cFormId, acTakeOver.SessionId, cEntity))
        spdlog::info("Actor {:X}: taken back, its last holder's ops run through {}", cFormId, acTakeOver.Tick);
    // Its holder's clip killed it there under the hold: its death is this game's (KillImpl 37896).
    if (acTakeOver.KillerId)
        m_actorBridge.KillActorBy(cFormId, Utils::GetFormIdBySessionId(acTakeOver.KillerId));
    return true;
}

void ClaimService::TakeNow(uint32_t aSessionId) noexcept
{
    std::erase_if(m_takeOvers, [&](const TakeOver& acTakeOver) { return acTakeOver.SessionId == aSessionId && Take(acTakeOver, true); });
}

void ClaimService::OnUpdate(const UpdateEvent&) noexcept
{
    RunGiveBacks();
    std::erase_if(m_takeOvers, [this](const TakeOver& acTakeOver) { return Take(acTakeOver, false); });
}

void ClaimService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_held.clear();
    m_giveBacks.clear();
    m_takeOvers.clear();
}
