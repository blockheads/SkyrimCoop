#include <Services/PairService.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/LocalComponent.h>
#include <Components/PlayerComponent.h>
#include <Components/RemoteComponent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PairKillEvent.h>
#include <Events/UpdateEvent.h>
#include <Games/Animation/GraphStream.h>
#include <Games/Animation/ReplayState.h>
#include <Messages/Relayed.h>
#include <Services/CharacterService.h>
#include <Services/HostService.h>
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
    Listen<PairKillEvent, &PairService::OnPairKill>(m_connections, aDispatcher, this);
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

void PairService::OnPairKill(const PairKillEvent& acEvent) noexcept
{
    for (Own& own : m_own)
        if (own.PartnerFormId == acEvent.FormId)
            own.Killed = true;
    if (m_actorBridge.IsActorCopy(acEvent.FormId))
        if (const auto cId = Utils::GetSessionIdByFormId(acEvent.FormId))
            m_killsSeen.insert(*cId);
}

void PairService::OnClaim(const NotifyPairClaim& acMessage) noexcept
{
    m_world.GetPairClaims().Set(acMessage.ActorId, {acMessage.PartnerId, acMessage.Tick, true});
    m_killsSeen.erase(acMessage.PartnerId);
    const entt::entity cPartner = EntityOf(acMessage.PartnerId);
    if (cPartner == entt::null || m_world.all_of<PlayerComponent>(cPartner))
    {
        for (Own& own : m_own)
            own.Granted |= own.ActorId == acMessage.ActorId && own.PartnerId == acMessage.PartnerId && own.Tick == acMessage.Tick;
        return;
    }
    const uint32_t cFormId = m_world.get<FormIdComponent>(cPartner).Id;
    auto& characters = m_world.GetCharacterService();
    for (Own& own : m_own)
    {
        if (own.ActorId != acMessage.ActorId || own.PartnerId != acMessage.PartnerId || own.Tick != acMessage.Tick)
            continue;
        // The NPC is this game's for the pair. Its kill stays held: its death is the host's.
        own.Granted = true;
        own.Lent = m_world.all_of<RemoteComponent>(cPartner) && characters.TakeOwnership(cFormId, acMessage.PartnerId, cPartner, true);
        // Not takeable here (a player's summon): the pair ends and the NPC goes back at once.
        if (!own.Lent && m_world.all_of<RemoteComponent>(cPartner))
        {
            spdlog::warn("Pair of {:X} with {:X} granted, but its partner cannot be this game's: ended here", own.ActorFormId, cFormId);
            m_actorBridge.ActorYieldPair(own.ActorFormId, cFormId);
            SendDone(own, acMessage.Tick);
        }
        return;
    }
    // Another game's pair with this game's NPC (the host's): its claimer's game simulates it for the pair.
    // Handed back by its last claimer and not taken yet: taken (and killed) first.
    if (!m_world.all_of<LocalComponent>(cPartner))
    {
        std::erase_if(m_backs, [&](const Back& acBack) {
            if (acBack.PartnerId == acMessage.PartnerId)
                characters.QueueTakeOver(acBack.PartnerId, acBack.Tick, acBack.KillerId);
            return acBack.PartnerId == acMessage.PartnerId;
        });
        characters.TakeOverNow(acMessage.PartnerId);
    }
    if (m_world.all_of<LocalComponent>(cPartner))
        characters.GiveUpOwnership(cPartner, acMessage.PartnerId, cFormId);
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
    const entt::entity cPartner = EntityOf(acMessage.PartnerId);
    if (cPartner == entt::null || m_world.all_of<PlayerComponent>(cPartner))
        return;
    auto& characters = m_world.GetCharacterService();
    const bool cHosting = m_world.GetHostService().IsHosting();
    if (!cHosting && m_world.all_of<LocalComponent>(cPartner))
        characters.GiveUpOwnership(cPartner, acMessage.PartnerId, m_world.get<FormIdComponent>(cPartner).Id);
    // The host's game takes it back once its copy played the claimer's pair to its end, then kills it if the clip did.
    else if (cHosting && m_world.all_of<RemoteComponent>(cPartner))
    {
        if (acMessage.Killed)
            m_backs.push_back({acMessage.PartnerId, acMessage.ActorId, acMessage.Tick, m_world.GetTick() + ReplayState::cPoseHoldMs});
        else
            characters.QueueTakeOver(acMessage.PartnerId, acMessage.Tick, 0);
    }
}

void PairService::SendDone(Own& aOwn, uint64_t aTick) noexcept
{
    PairDoneRequest request;
    request.ActorId = aOwn.ActorId;
    request.PartnerId = aOwn.PartnerId;
    request.Tick = aTick;
    // Its KillActor came, or KillMoveEnd's kill was counted under the hold (38614, 38617).
    request.Killed = aOwn.Lent && (aOwn.Killed || m_actorBridge.ActorKillCounted(aOwn.PartnerFormId));
    // Giving the partner up drops its unsent graph ops: they go first (mounts.md, "A horse handed back between pairs").
    if (aOwn.Lent)
        m_world.GetCharacterService().SendGraphStream();
    m_transport.Send(request);
    aOwn.Sent = true;
}

void PairService::OnUpdate(const UpdateEvent&) noexcept
{
    auto& streams = m_world.GetGraphStreams();
    for (Own& own : m_own)
    {
        if (!own.Granted || own.Sent)
            continue;
        const auto cUpdates = own.Lent ? streams.Updates(own.PartnerFormId) : std::nullopt;
        if (!own.Ended)
        {
            const auto cActor = m_actorBridge.GetActorStateFields(own.ActorFormId);
            const auto cPartner = m_actorBridge.GetActorStateFields(own.PartnerFormId);
            if (!ReplayState::PairDone(own.Lent, own.Killed, cPartner.InKillMove || cPartner.InPair, cActor.InKillMove || cActor.InPair))
                continue;
            own.Ended = true;
            own.Since = cUpdates ? cUpdates->Count : 0;
        }
        // A killed partner holds its pose, its graph stopped: its ops so far are all. One that left the pair alive
        // goes back with its next update (its PairEnd is due with it), as a mount.
        if (!own.Lent || !cUpdates || own.Killed || cUpdates->Count > own.Since)
            SendDone(own, cUpdates ? cUpdates->LastTick : 0);
    }
    std::erase_if(m_backs, [&](const Back& acBack) {
        if (!m_killsSeen.contains(acBack.PartnerId) && m_world.GetTick() < acBack.Until)
            return false;
        m_killsSeen.erase(acBack.PartnerId);
        m_world.GetCharacterService().QueueTakeOver(acBack.PartnerId, acBack.Tick, acBack.KillerId);
        return true;
    });
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
    m_backs.clear();
    m_killsSeen.clear();
    m_world.GetPairClaims().Clear();
}
