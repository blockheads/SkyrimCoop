#include <Services/ClaimService.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/LocalComponent.h>
#include <Components/RemoteComponent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Games/Animation/GraphStream.h>
#include <Messages/Relayed.h>
#include <Services/CharacterService.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>

namespace
{
constexpr uint64_t cUnheardClaimMs = 5000; // a give-back asked before its claim was heard waits this long for it
constexpr uint64_t cStalledMs = 3000;      // a graph that stops updating: given back through its last recorded update
}

ClaimService::ClaimService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
{
    Listen<NotifyClaim, &ClaimService::OnClaim>(m_connections, aDispatcher, this);
    Listen<NotifyClaimRefused, &ClaimService::OnRefused>(m_connections, aDispatcher, this);
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

namespace
{
std::vector<uint32_t> MemberIds(const Payloads::Claim& acClaim)
{
    std::vector<uint32_t> ids;
    for (const auto& cMember : acClaim.Members)
        ids.push_back(cMember.ActorId);
    return ids;
}
} // namespace

void ClaimService::OnRefused(const NotifyClaimRefused& acMessage) noexcept
{
    m_world.GetActionClaims().Set(acMessage.ForId, {acMessage.Tick, 0, MemberIds(acMessage), false, acMessage.Kind});
}

void ClaimService::OnClaim(const NotifyClaim& acMessage) noexcept
{
    m_world.GetActionClaims().Set(acMessage.ForId, {acMessage.Tick, acMessage.ClaimId, MemberIds(acMessage), true, acMessage.Kind});
    const entt::entity cFor = EntityOf(acMessage.ForId);
    const bool cOurs = cFor != entt::null && m_world.all_of<LocalComponent>(cFor);
    for (const auto& cMember : acMessage.Members)
    {
        if (cOurs)
        {
            TakeMember(cMember, acMessage);
            continue;
        }
        // Another game's (a passenger driving on): ours to give back no longer, a taken one a copy here.
        m_held.erase(cMember.ActorId);
        std::erase_if(m_giveBacks, [&](const Pending& acPending) { return acPending.SessionId == cMember.ActorId; });
        const entt::entity cEntity = EntityOf(cMember.ActorId);
        if (cMember.Take && cEntity != entt::null && m_world.all_of<LocalComponent>(cEntity))
            m_world.GetCharacterService().GiveUpOwnership(cEntity, cMember.ActorId, m_world.get<FormIdComponent>(cEntity).Id);
    }
}

void ClaimService::TakeMember(const Payloads::ClaimMember& acMember, const NotifyClaim& acClaim) noexcept
{
    Held& held = m_held[acMember.ActorId];
    held.ClaimId = acClaim.ClaimId;
    for (Pending& pending : m_giveBacks)
        if (pending.SessionId == acMember.ActorId && !pending.ClaimId)
            pending.ClaimId = acClaim.ClaimId;
    if (!acMember.Take)
        return; // locked: busy, its owner's still
    const entt::entity cEntity = EntityOf(acMember.ActorId);
    if (cEntity == entt::null)
    {
        // Gone from this game since it asked (a cell change): back at once, nobody left to simulate it here.
        spdlog::warn("Claim {} of {:X}: not on this game, given back", acClaim.ClaimId, acMember.ActorId);
        GiveBack(acMember.ActorId, false, true);
        m_held.erase(acMember.ActorId);
        return;
    }
    if (!m_world.all_of<RemoteComponent>(cEntity))
        return; // ours already (the host's own ride)
    const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
    held.Took = m_world.GetCharacterService().TakeOwnership(cFormId, acMember.ActorId, cEntity, acMember.DeathOnGiveBack);
    if (held.Took)
    {
        // A give-back asked before this claim came: this game's actor to read from now.
        const auto cUpdates = m_world.GetGraphStreams().Updates(cFormId);
        for (Pending& pending : m_giveBacks)
            if (pending.SessionId == acMember.ActorId && !pending.FormId)
            {
                pending.FormId = cFormId;
                pending.Since = cUpdates ? cUpdates->Count : 0;
            }
        return;
    }
    // Not takeable here (a player's summon): back at once.
    spdlog::warn("Claim {} of {:X}: this game cannot take it, given back", acClaim.ClaimId, cFormId);
    GiveBack(acMember.ActorId, false, true);
    m_held.erase(acMember.ActorId);
}

void ClaimService::GiveBack(uint32_t aSessionId, bool aKilled, bool aNow, std::function<void()> aBefore) noexcept
{
    const entt::entity cEntity = EntityOf(aSessionId);
    const uint32_t cFormId = cEntity != entt::null && m_world.all_of<LocalComponent>(cEntity) ? m_world.get<FormIdComponent>(cEntity).Id : 0;
    const auto cUpdates = cFormId ? m_world.GetGraphStreams().Updates(cFormId) : std::nullopt;
    const auto cHeld = m_held.find(aSessionId);
    m_giveBacks.push_back({aSessionId, cHeld != m_held.end() ? cHeld->second.ClaimId : 0, m_world.GetTick(), cFormId,
                           cUpdates ? cUpdates->Count : 0, aKilled, aNow, false, std::move(aBefore)});
    if (aNow)
        RunGiveBacks();
}

void ClaimService::RunGiveBacks() noexcept
{
    std::vector<Pending> due;
    std::erase_if(m_giveBacks, [&](Pending& aPending) {
        if (!aPending.ClaimId)
        {
            if (m_world.GetTick() < aPending.Asked + cUnheardClaimMs)
                return false;
            spdlog::info("Give-back of {:X}: its claim never came", aPending.SessionId);
            return true;
        }
        const auto cUpdates = aPending.FormId ? m_world.GetGraphStreams().Updates(aPending.FormId) : std::nullopt;
        const bool cStalled = m_world.GetTick() >= aPending.Asked + cStalledMs;
        if (!aPending.Now && (!aPending.Waited || (cUpdates && cUpdates->Count <= aPending.Since && !cStalled)))
            return false;
        due.push_back(std::move(aPending));
        return true;
    });
    for (Pending& pending : due)
    {
        const auto cUpdates = pending.FormId ? m_world.GetGraphStreams().Updates(pending.FormId) : std::nullopt;
        ActorGiveBackRequest request;
        request.ActorId = pending.SessionId;
        request.ClaimId = pending.ClaimId;
        // A kill its hold counted while it waited (38614's queued one) still counts (DeferKill 38617).
        request.Killed = pending.Killed || (pending.FormId && m_actorBridge.ActorKillCounted(pending.FormId));
        // Giving it up drops its unsent ops: they go first, the give-back's tick their last update (copies take it then).
        request.Tick = cUpdates ? cUpdates->LastTick : 0;
        if (pending.FormId)
            request.Whereabouts = m_world.GetCharacterService().ReadWhereabouts(pending.FormId);
        m_world.GetCharacterService().SendGraphStream();
        if (pending.Before)
            pending.Before();
        m_transport.Send(request);
    }
}

void ClaimService::OnGiveBack(const NotifyActorGiveBack& acMessage) noexcept
{
    if (acMessage.Ended)
        m_world.GetActionClaims().End(acMessage.ClaimId);
    const auto cHeld = m_held.find(acMessage.ActorId);
    const bool cTook = cHeld != m_held.end() && cHeld->second.Took;
    if (cHeld != m_held.end())
        m_held.erase(cHeld);
    std::erase_if(m_giveBacks, [&](const Pending& acPending) { return acPending.SessionId == acMessage.ActorId; });
    const entt::entity cEntity = EntityOf(acMessage.ActorId);
    if (cEntity == entt::null)
        return;
    const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
    // Taken for the claim: a copy again. A copy here goes where the holder left it (the host's game takes it later).
    if (m_world.all_of<LocalComponent>(cEntity))
    {
        if (cTook)
            m_world.GetCharacterService().GiveUpOwnership(cEntity, acMessage.ActorId, cFormId);
    }
    else if (acMessage.Taken && acMessage.Whereabouts.Where == ActorWhereabouts::State::kPlaced)
        m_world.GetCharacterService().FollowOwner(cEntity, cFormId, acMessage.Whereabouts, false);
}

void ClaimService::OnUpdate(const UpdateEvent&) noexcept
{
    RunGiveBacks();
    for (Pending& pending : m_giveBacks)
        pending.Waited = true;
}

void ClaimService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_held.clear();
    m_giveBacks.clear();
    m_world.GetActionClaims().Clear();
}
