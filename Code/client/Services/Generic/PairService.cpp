#include <Services/PairService.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/PlayerComponent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Games/Animation/ReplayState.h>
#include <Messages/Relayed.h>
#include <Services/ClaimService.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <Structs/ActionEvent.h>
#include <World.h>

namespace
{
constexpr uint64_t cSwingWaitMs = 2000; // a yielded clip that never leaves: no plain swing
}

PairService::PairService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
{
    Listen<ActionEvent, &PairService::OnAction>(m_connections, aDispatcher, this);
    Listen<NotifyPairClaim, &PairService::OnClaim>(m_connections, aDispatcher, this);
    Listen<NotifyPairRefused, &PairService::OnRefused>(m_connections, aDispatcher, this);
    Listen<NotifyPairDone, &PairService::OnDone>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &PairService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &PairService::OnDisconnected>(m_connections, aDispatcher, this);
}

entt::entity PairService::EntityOf(uint32_t aSessionId) const noexcept
{
    for (const auto cEntity : m_world.view<FormIdComponent>())
        if (Utils::GetSessionId(cEntity) == aSessionId)
            return cEntity;
    return entt::null;
}

// An accepted action of this game's own actor with a target event: 32803 started a pair (38949). A kill move on
// another game's actor is claimed at once; the pair plays on meanwhile.
void PairService::OnAction(const ActionEvent& acAction) noexcept
{
    if (!m_transport.IsConnected() || acAction.TargetEventName.empty() || !ReplayState::EntersKillMove(acAction.EventName))
        return;
    const auto cActorId = Utils::GetSessionIdByFormId(acAction.ActorId);
    const auto cPartnerId = Utils::GetSessionIdByFormId(acAction.TargetId);
    if (!cActorId || !cPartnerId || m_actorBridge.IsActorCopy(acAction.ActorId) || !m_actorBridge.IsActorCopy(acAction.TargetId))
        return;
    m_own.push_back({*cActorId, *cPartnerId, acAction.ActorId, acAction.TargetId, acAction.ActionId, acAction.Tick});
    PairClaimRequest request;
    request.ActorId = *cActorId;
    request.PartnerId = *cPartnerId;
    request.Tick = acAction.Tick;
    m_transport.Send(request);
}

void PairService::OnClaim(const NotifyPairClaim& acMessage) noexcept
{
    m_world.GetPairClaims().Set(acMessage.ActorId, {acMessage.PartnerId, acMessage.Tick, true});
    const entt::entity cPartner = EntityOf(acMessage.PartnerId);
    const bool cNpc = cPartner != entt::null && !m_world.all_of<PlayerComponent>(cPartner);
    for (Own& own : m_own)
    {
        if (own.ActorId != acMessage.ActorId || own.PartnerId != acMessage.PartnerId || own.Tick != acMessage.Tick)
            continue;
        own.Granted = true;
        // An NPC partner was claimed for this game just before (NotifyActorClaim), its kill held: its death the host's.
        own.Lent = cNpc && m_world.ctx().at<ClaimService>().Holds(acMessage.PartnerId);
        // Not taken here (a player's summon: given back already): the pair ends.
        if (cNpc && !own.Lent)
        {
            spdlog::warn("Pair of {:X} with {:X} granted, but its partner is not this game's: ended here", own.ActorFormId, own.PartnerFormId);
            m_actorBridge.ActorYieldPair(own.ActorFormId, own.PartnerFormId);
            SendDone(own);
        }
        return;
    }
}

void PairService::OnRefused(const NotifyPairRefused& acMessage) noexcept
{
    m_world.GetPairClaims().Set(acMessage.ActorId, {acMessage.PartnerId, acMessage.Tick, false});
    const auto cOwn = std::ranges::find_if(m_own, [&](const Own& acOwn) {
        return acOwn.ActorId == acMessage.ActorId && acOwn.PartnerId == acMessage.PartnerId && acOwn.Tick == acMessage.Tick;
    });
    if (cOwn == m_own.end())
        return;
    spdlog::info("Pair of {:X} with {:X} refused by the host ({}): ended here", cOwn->ActorFormId, cOwn->PartnerFormId, acMessage.Reason);
    m_actorBridge.ActorYieldPair(cOwn->ActorFormId, cOwn->PartnerFormId);
    // A partner that cannot pair gets the engine's own fallback, a plain swing (42350); a dead one nothing.
    if (acMessage.Reason == Payloads::Pair::kCannotPair && !m_actorBridge.IsActorDead(cOwn->PartnerFormId))
        m_swings.push_back({cOwn->ActorFormId, cOwn->ActionId, m_world.GetTick() + cSwingWaitMs});
    m_own.erase(cOwn);
}

void PairService::OnDone(const NotifyPairDone& acMessage) noexcept
{
    m_world.GetPairClaims().Done(acMessage.ActorId);
    std::erase_if(m_own, [&](const Own& acOwn) { return acOwn.ActorId == acMessage.ActorId && acOwn.PartnerId == acMessage.PartnerId; });
}

void PairService::SendDone(Own& aOwn) noexcept
{
    aOwn.Sent = true;
    PairDoneRequest request;
    request.ActorId = aOwn.ActorId;
    request.PartnerId = aOwn.PartnerId;
    request.Tick = aOwn.Tick;
    if (!aOwn.Lent)
    {
        m_transport.Send(request);
        return;
    }
    // The partner goes back after the done: killed (its KillActor 42838 or KillMoveEnd 38614's kill counted under the
    // hold, 38617) at once, its graph stopped; alive with its next update, its PairEnd due with it.
    const bool cKilled = m_actorBridge.ActorKillCounted(aOwn.PartnerFormId);
    m_world.ctx().at<ClaimService>().GiveBack(aOwn.PartnerId, cKilled, cKilled, [this, request] { m_transport.Send(request); });
}

void PairService::OnUpdate(const UpdateEvent&) noexcept
{
    for (Own& own : m_own)
    {
        if (!own.Granted || own.Sent)
            continue;
        const auto cActor = m_actorBridge.GetActorStateFields(own.ActorFormId);
        const auto cPartner = m_actorBridge.GetActorStateFields(own.PartnerFormId);
        const bool cKilled = own.Lent && m_actorBridge.ActorKillCounted(own.PartnerFormId);
        if (ReplayState::PairDone(own.Lent, cKilled, cPartner.InKillMove || cPartner.InPair, cActor.InKillMove || cActor.InPair))
            SendDone(own);
    }
    std::erase_if(m_swings, [&](const Swing& acSwing) {
        const auto cState = m_actorBridge.GetActorStateFields(acSwing.FormId);
        if (m_world.GetTick() > acSwing.Until)
        {
            spdlog::info("Pair refused: {:X} never swung plainly (in pair {}, in kill move {})", acSwing.FormId, cState.InPair, cState.InKillMove);
            return true;
        }
        if (cState.InPair || cState.InKillMove || !m_actorBridge.ActorPerformPlainly(acSwing.FormId, acSwing.ActionId))
            return false;
        spdlog::info("Pair refused: {:X} swings plainly (action {:X})", acSwing.FormId, acSwing.ActionId);
        return true;
    });
}

void PairService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_own.clear();
    m_swings.clear();
    m_world.GetPairClaims().Clear();
}
