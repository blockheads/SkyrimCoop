#include <Services/KillMoveService.h>

#include <Bridges/IActorBridge.h>
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

KillMoveService::KillMoveService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
{
    Listen<ActionEvent, &KillMoveService::OnAction>(m_connections, aDispatcher, this);
    Listen<NotifyClaim, &KillMoveService::OnClaim>(m_connections, aDispatcher, this);
    Listen<NotifyClaimRefused, &KillMoveService::OnRefused>(m_connections, aDispatcher, this);
    Listen<NotifyActorGiveBack, &KillMoveService::OnGiveBack>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &KillMoveService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &KillMoveService::OnDisconnected>(m_connections, aDispatcher, this);
}

// An accepted action of this game's own actor with a target event: 32803 started a pair (38949). A kill move on
// another game's actor is claimed at once; the pair plays on meanwhile.
void KillMoveService::OnAction(const ActionEvent& acAction) noexcept
{
    if (!m_transport.IsConnected() || acAction.TargetEventName.empty() || !ReplayState::EntersKillMove(acAction.EventName))
        return;
    const auto cActorId = Utils::GetSessionIdByFormId(acAction.ActorId);
    const auto cPartnerId = Utils::GetSessionIdByFormId(acAction.TargetId);
    if (!cActorId || !cPartnerId || m_actorBridge.IsActorCopy(acAction.ActorId) || !m_actorBridge.IsActorCopy(acAction.TargetId))
        return;
    m_own.push_back({*cActorId, *cPartnerId, acAction.ActorId, acAction.TargetId, acAction.ActionId, acAction.Tick});
    // An NPC victim is taken, its death the host's (its deferred kill kept); a player victim never changes owner.
    const bool cNpc = !m_actorBridge.IsOtherPlayer(acAction.TargetId);
    ClaimRequest request;
    request.Kind = Payloads::Claim::kKillMove;
    request.ForId = *cActorId;
    request.Tick = acAction.Tick;
    request.Members = {{*cActorId, false, false}, {*cPartnerId, cNpc, cNpc}};
    m_transport.Send(request);
}

void KillMoveService::OnClaim(const NotifyClaim& acMessage) noexcept
{
    if (acMessage.Kind != Payloads::Claim::kKillMove)
        return;
    for (Own& own : m_own)
    {
        if (own.ActorId != acMessage.ForId || own.Tick != acMessage.Tick)
            continue;
        own.Granted = true;
        own.ClaimId = acMessage.ClaimId;
        // Its NPC victim taken just before (ClaimService, this message's first sink), its kill held: its death the host's.
        const auto cVictim = std::ranges::find(acMessage.Members, own.PartnerId, &Payloads::ClaimMember::ActorId);
        const bool cToTake = cVictim != acMessage.Members.end() && cVictim->Take;
        own.Taken = cToTake && m_world.ctx().at<ClaimService>().Holds(own.PartnerId);
        // Not taken here (a player's summon, or gone: given back already): the pair ends.
        if (cToTake && !own.Taken)
        {
            spdlog::warn("Kill move of {:X} on {:X} granted, but its victim is not this game's: ended here", own.ActorFormId, own.PartnerFormId);
            m_actorBridge.ActorYieldPair(own.ActorFormId, own.PartnerFormId);
            SendDone(own);
        }
        return;
    }
}

void KillMoveService::OnRefused(const NotifyClaimRefused& acMessage) noexcept
{
    if (acMessage.Kind != Payloads::Claim::kKillMove)
        return;
    const auto cOwn = std::ranges::find_if(m_own, [&](const Own& acOwn) { return acOwn.ActorId == acMessage.ForId && acOwn.Tick == acMessage.Tick; });
    if (cOwn == m_own.end())
        return;
    spdlog::info("Kill move of {:X} on {:X} refused by the host ({}): ended here", cOwn->ActorFormId, cOwn->PartnerFormId, acMessage.Reason);
    m_actorBridge.ActorYieldPair(cOwn->ActorFormId, cOwn->PartnerFormId);
    // A victim that cannot pair gets the engine's own fallback, a plain swing (42350); a dead one nothing. One in
    // another claim (crossed kill moves): the blow is dropped.
    if (acMessage.Reason == Payloads::Claim::kCannotTake && !m_actorBridge.IsActorDead(cOwn->PartnerFormId))
        m_swings.push_back({cOwn->ActorFormId, cOwn->ActionId, m_world.GetTick() + cSwingWaitMs});
    m_own.erase(cOwn);
}

void KillMoveService::OnGiveBack(const NotifyActorGiveBack& acMessage) noexcept
{
    if (acMessage.Ended)
        std::erase_if(m_own, [&](const Own& acOwn) { return acOwn.ClaimId && acOwn.ClaimId == acMessage.ClaimId; });
}

void KillMoveService::SendDone(Own& aOwn) noexcept
{
    aOwn.Sent = true;
    auto& claims = m_world.ctx().at<ClaimService>();
    // Locks at once; a taken victim after its next update, or at once if its kill counted (kill-moves.md).
    claims.GiveBack(aOwn.ActorId, false, true);
    const bool cKilled = aOwn.Taken && m_actorBridge.ActorKillCounted(aOwn.PartnerFormId);
    claims.GiveBack(aOwn.PartnerId, cKilled, !aOwn.Taken || cKilled);
}

void KillMoveService::OnUpdate(const UpdateEvent&) noexcept
{
    for (Own& own : m_own)
    {
        if (!own.Granted || own.Sent)
            continue;
        const auto cActor = m_actorBridge.GetActorStateFields(own.ActorFormId);
        const auto cPartner = m_actorBridge.GetActorStateFields(own.PartnerFormId);
        const bool cKilled = own.Taken && m_actorBridge.ActorKillCounted(own.PartnerFormId);
        if (ReplayState::PairDone(own.Taken, cKilled, cPartner.InKillMove || cPartner.InPair, cActor.InKillMove || cActor.InPair))
            SendDone(own);
    }
    std::erase_if(m_swings, [&](const Swing& acSwing) {
        const auto cState = m_actorBridge.GetActorStateFields(acSwing.FormId);
        if (m_world.GetTick() > acSwing.Until)
        {
            spdlog::info("Kill move refused: {:X} never swung plainly (in pair {}, in kill move {})", acSwing.FormId, cState.InPair, cState.InKillMove);
            return true;
        }
        if (cState.InPair || cState.InKillMove || !m_actorBridge.ActorPerformPlainly(acSwing.FormId, acSwing.ActionId))
            return false;
        spdlog::info("Kill move refused: {:X} swings plainly (action {:X})", acSwing.FormId, acSwing.ActionId);
        return true;
    });
}

void KillMoveService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_own.clear();
    m_swings.clear();
}
