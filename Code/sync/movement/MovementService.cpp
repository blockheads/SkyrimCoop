// Movement snapshots: this game's actors out each send round, their copies played back each frame.
#include <movement/MovementService.h>
#include <animation/GraphStreamService.h>
#include <mounts/MountService.h>
#include <core/TransportService.h>
#include <core/SessionService.h>

#include <animation/LocalAnimationComponent.h>
#include <animation/RemoteAnimationComponent.h>
#include <core/FormIdComponent.h>
#include <movement/InterpolationComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>
#include <characters/WaitingFor3D.h>
#include <characters/OwnerPlaceComponent.h>
#include <characters/HeldCopyComponent.h>

#include <characters/Copy3DEvent.h>
#include <characters/CopyHeldEvent.h>
#include <animation/ReplayState.h>
#include <characters/ReferenceJumpEvent.h>

#include <Structs/ActionEvent.h>
#include <Messages/NotifyReferencesMove.h>
#include <Messages/ReferencesMoveRequest.h>
#include <physics/BodyService.h>

#include <ports/IActorBridge.h>
#include <ports/IFormBridge.h>
#include <ports/IPlayerBridge.h>

#include <movement/SnapshotAge.h>
#include <movement/CopyStep.h>
#include <movement/InterpolationSystem.h>
#include <animation/AnimationSystem.h>

#include <core/World.h>
#include <core/SessionIds.h>
#include <core/FormIds.h>

#include <Messages/Relayed.h>
#include <animation/GraphStream.h>
#include <travel/TravelService.h>
#include <characters/CharacterInternal.h>

MovementService::MovementService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge,
                                 IPlayerBridge& aPlayerBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_playerBridge(aPlayerBridge)
{
    Listen<NotifyReferencesMove, &MovementService::OnReferencesMoveRequest>(m_connections, aDispatcher, this);
    Listen<ReferenceJumpEvent, &MovementService::OnReferenceJump>(m_connections, aDispatcher, this);
}

void MovementService::OnReferencesMoveRequest(const NotifyReferencesMove& acMessage) const noexcept
{
    const auto& actors = m_world.GetActorIndex();
    auto& clock = m_world.GetCopyClock();
    clock.OnSnapshot(acMessage.Tick, m_transport.GetTick());

    for (const auto& [sessionId, update] : acMessage.Updates)
    {
        const auto cCopy = actors.BySession<RemoteComponent, InterpolationComponent, RemoteAnimationComponent>(sessionId);
        if (cCopy == entt::null)
            continue;

        auto& interpolationComponent = m_world.get<InterpolationComponent>(cCopy);
        auto& animationComponent = m_world.get<RemoteAnimationComponent>(cCopy);
        const auto& movement = update.UpdatedMovement;
        for (const auto& action : update.ActionEvents)
            animationComponent.TimePoints.push_back(action);
        // Sent before its owner's last jump, arrived after it: an old place (actor-movement.md, "A goal outlives its place").
        if (SnapshotAge::IsStale(acMessage.Tick, interpolationComponent.RestartTick))
            continue;

        InterpolationComponent::TimePoint point;
        point.Tick = acMessage.Tick;
        point.Position = movement.Position;
        point.Rotation = {movement.Rotation.x, 0.f, movement.Rotation.y};
        point.Variables = movement.Variables;
        point.Direction = movement.Direction;
        point.Lying = movement.Lying;
        point.Knocked = movement.Knocked;
        point.OnGround = movement.OnGround;
        point.CapsuleCollides = movement.CapsuleCollides;
        point.BodiesLayer = movement.BodiesLayer;
        point.GraphsStepPhysics = movement.GraphsStepPhysics;
        // Put there at once, or into another interior or worldspace: the copy starts there (RunPresence), never
        // sweeps across.
        bool teleported = movement.Teleported;
        const auto& points = interpolationComponent.TimePoints;
        if (movement.CellId && (points.empty() || points.back().Tick <= point.Tick))
        {
            auto& place = m_world.get_or_emplace<OwnerPlaceComponent>(cCopy);
            teleported |= place.CellId && (movement.WorldSpaceId != place.WorldSpaceId || (!movement.WorldSpaceId && movement.CellId != place.CellId));
            place.CellId = movement.CellId;
            place.WorldSpaceId = movement.WorldSpaceId;
        }
        interpolationComponent.Playback.OnPoint(point.Tick);
        if (teleported)
        {
            InterpolationSystem::Restart(interpolationComponent, movement.Position, acMessage.Tick, m_transport.GetTick());
            interpolationComponent.Playback.Snap(clock);
        }
        InterpolationSystem::AddPoint(interpolationComponent, point);
        if (interpolationComponent.TimePoints.size() == 1)
            interpolationComponent.Position = point.Position; // shown there until the second (HoldCopy)
    }
}

void MovementService::RunLocalUpdates() noexcept
{
    // 30 a second for players, mounts and fights, whose copies are watched closely; 10 for the rest (copy-clock.md).
    // Timed by the frame clock, half a frame early: at 60 fps every second frame, not every third.
    constexpr double cRoundMs = 33.0;
    constexpr uint64_t cSlowRounds = 3;

    const uint64_t cNow = m_world.GetFrameTick();
    if (static_cast<double>(cNow - m_lastSnapshot) + m_world.GetCopyClock().FrameMs() / 2.0 < cRoundMs)
        return;

    m_lastSnapshot = cNow;
    ++m_round;
    const uint32_t cPlayer = m_playerBridge.GetPlayerFormId();

    ReferencesMoveRequest message;
    message.Tick = cNow; // when this frame's positions were, not when we read the clock (S10)

    auto animatedLocalView = m_world.view<LocalComponent, LocalAnimationComponent, FormIdComponent>();

    std::unordered_set<uint32_t> waiting; // ours not due yet: their jumps go with their next snapshot
    for (auto entity : animatedLocalView)
    {
        auto& localComponent = animatedLocalView.get<LocalComponent>(entity);
        auto& animationComponent = animatedLocalView.get<LocalAnimationComponent>(entity);
        auto& formIdComponent = animatedLocalView.get<FormIdComponent>(entity);
        const uint32_t cFormId = formIdComponent.Id;
        // Its graph's inputs, for its copies (graph-input-stream.md): this game's NPCs and its player. Recorded on
        // through its death; one found dead is not started.
        if (!m_actorBridge.IsActorDead(cFormId) || m_world.GetGraphStreams().Recording(cFormId))
            m_world.GetGraphStreams().Record(cFormId, localComponent.Id);
        const bool cWatched = cFormId == cPlayer || cFormId == m_world.ctx().at<MountService>().RiddenMount() || m_actorBridge.GetActorAiFields(cFormId).CombatState != 0;
        if (!cWatched && localComponent.LastSentRound && m_round - localComponent.LastSentRound < cSlowRounds)
        {
            waiting.insert(cFormId);
            continue;
        }
        localComponent.LastSentRound = m_round;

        AnimationSystem::Serialize(m_world, message, localComponent, animationComponent, formIdComponent);
        const auto it = message.Updates.find(localComponent.Id);
        if (m_jumped.erase(formIdComponent.Id) && it != message.Updates.end())
            it->second.UpdatedMovement.Teleported = true;
        if (it != message.Updates.end())
        {
            it->second.UpdatedMovement.Lying = m_actorBridge.IsLyingInRagdoll(formIdComponent.Id);
            it->second.UpdatedMovement.Knocked = m_actorBridge.IsKnockedDown(formIdComponent.Id);
            it->second.UpdatedMovement.OnGround = m_actorBridge.IsOnGround(formIdComponent.Id);
            it->second.UpdatedMovement.CapsuleCollides = m_actorBridge.CapsuleCollides(formIdComponent.Id);
            it->second.UpdatedMovement.BodiesLayer = m_actorBridge.BodiesLayer(formIdComponent.Id);
            it->second.UpdatedMovement.GraphsStepPhysics = m_actorBridge.GraphsStepPhysics(formIdComponent.Id);
        }
    }
    std::erase_if(m_jumped, [&](uint32_t aFormId) { return !waiting.contains(aFormId); }); // and those not ours

    if (!message.Updates.empty())
        m_transport.Send(message);
    m_world.ctx().at<GraphStreamService>().Send();
}

// Until its owner's snapshots say where it goes: at its owner's last place once in its cell, else where it is
// (RunPresence moves it), no goal to step to (actor-movement.md, "A goal outlives its place").
void MovementService::HoldCopy(entt::entity aEntity, uint32_t aFormId) const noexcept
{
    glm::vec3 at{}, rotation{};
    m_actorBridge.GetActorPosition(aFormId, at.x, at.y, at.z);
    // By the cell alone: a spawn says no worldspace (RefreshCopy keeps the last one's).
    const auto* pPlace = m_world.try_get<OwnerPlaceComponent>(aEntity);
    if (pPlace && pPlace->CellId && m_actorBridge.GetActorParentCellFormId(aFormId) == ToGame(pPlace->CellId))
    {
        const auto& cInterpolation = m_world.get<InterpolationComponent>(aEntity);
        if (!cInterpolation.TimePoints.empty() || cInterpolation.RestartTick)
            at = cInterpolation.Position;
    }
    m_actorBridge.GetActorRotation(aFormId, rotation.x, rotation.y, rotation.z);
    const float cAt[3]{at.x, at.y, at.z};
    const float cRotation[3]{rotation.x, rotation.y, rotation.z};
    static const AnimationVariables s_none;
    const CopyMove cMove = CopyStep::Hold(m_actorBridge.IsLyingInRagdoll(aFormId), m_actorBridge.GetActorMount(aFormId) != 0);
    m_actorBridge.ApplyRemoteMovement(aFormId, cAt, cRotation, 0.f, s_none, cMove);
}

void MovementService::RunRemoteUpdates() noexcept
{
    // The frame clock (S10): a copy's ease out of its ragdoll steps steadily.
    const auto currentTick = m_world.GetFrameTick();

    // Interpolation has to keep running even if the actor is not in view, otherwise we will never know if we need to spawn it
    auto interpolatedEntities = m_world.view<RemoteComponent, InterpolationComponent>();

    for (auto entity : interpolatedEntities)
    {
        auto* pFormIdComponent = m_world.try_get<FormIdComponent>(entity);
        auto& interpolationComponent = interpolatedEntities.get<InterpolationComponent>(entity);
        const uint64_t tick = InterpolationSystem::PlaybackTick(m_world, entity);

        auto& points = interpolationComponent.TimePoints;
        if (points.size() < 2)
        {
            if (pFormIdComponent)
                HoldCopy(entity, pFormIdComponent->Id);
            continue;
        }
        while (points.size() > 2 && tick > std::next(points.begin())->Tick)
            points.pop_front();
        const auto& first = points.front();
        const auto& second = *std::next(points.begin());
        glm::vec3 position{};
        glm::vec3 rotation{};
        // Late snapshots: carried on for up to one send interval (Source's and Unity's short extrapolation).
        const auto cAhead = std::min<uint64_t>(static_cast<uint64_t>(interpolationComponent.Playback.Interval()), 100);
        if (!InterpolationSystem::Interpolate(first, second, tick, position, rotation, cAhead))
            continue;
        // A newer snapshot ended its guess: from where the guess showed it, blended (copy-clock.md, "A late snapshot").
        auto& guess = interpolationComponent.Guess;
        auto& correction = interpolationComponent.Correction;
        if (guess && guess->second.Tick != second.Tick)
        {
            glm::vec3 guessed{}, guessedRotation{};
            if (InterpolationSystem::Interpolate(guess->first, guess->second, tick, guessed, guessedRotation, cAhead))
                correction.Begin(guessed + correction.At(tick) - position, tick);
            guess.reset();
        }
        if (tick > second.Tick && cAhead)
            guess.emplace(first, second);
        position += correction.At(tick);
        interpolationComponent.Position = position;
        // A copy on a mount: seated, its seat carries it (player-mount.md); getting off, off the horse at once
        // (FinishDismount 38696).
        const uint32_t cMount = pFormIdComponent ? m_actorBridge.GetActorMount(pFormIdComponent->Id) : 0;
        if (cMount)
        {
            const uint8_t cSit = m_actorBridge.GetActorSitState(pFormIdComponent->Id);
            if (cSit == ReplayState::kSitSeated)
            {
                static const AnimationVariables s_still;
                const float cHere[3]{position.x, position.y, position.z};
                m_actorBridge.ApplyRemoteMovement(pFormIdComponent->Id, cHere, cHere, 0.f, s_still, CopyMove::Leave);
                continue;
            }
            if (cSit == ReplayState::kSitWantsToStand)
                m_actorBridge.ActorFinishDismount(pFormIdComponent->Id);
        }
        if (pFormIdComponent)
        {
            // Booleans and integers: the current snapshot's, never older (graphs react to their changes). Floats
            // (Direction, Speed): while it stands, the nearest snapshot's, what moved its owner there, so a strafe's
            // animation turns with the copy's movement, within half an interval (S10, copy-clock.md).
            const AnimationVariables* pVariables = &first.Variables;
            if (auto* pAnimation = m_world.try_get<RemoteAnimationComponent>(entity))
            {
                if (first.Tick > pAnimation->VariablesTick && HasVariables(first.Variables))
                {
                    pAnimation->Variables = first.Variables;
                    pAnimation->VariablesTick = first.Tick;
                }
                pVariables = &pAnimation->Variables;
            }
            const bool cStanding = !first.Lying && !second.Lying && !first.Knocked && !second.Knocked;
            const bool cNearerSecond = cStanding && (tick >= second.Tick || (tick > first.Tick && 2 * (tick - first.Tick) >= second.Tick - first.Tick));
            AnimationVariables shown;
            if (cNearerSecond && second.Variables.Floats.size() == pVariables->Floats.size() && second.Variables.Floats != pVariables->Floats)
            {
                shown = *pVariables;
                shown.Floats = second.Variables.Floats;
                pVariables = &shown;
            }
            // Its owner lying, or the copy itself: its own ragdoll places it. Up again, it is eased from where its
            // engine stood it onto its owner's place (physics-sync.md, "A copy's ragdoll, steered").
            auto& ease = interpolationComponent.Ease;
            const bool cOwnerLies = tick >= second.Tick ? second.Lying : first.Lying;
            const bool cOwnerKnocked = tick >= second.Tick ? second.Knocked : first.Knocked;
            if (!cOwnerKnocked && !cOwnerLies)
                m_world.ctx().at<BodyService>().EndRagdoll(m_world.get<RemoteComponent>(entity).Id,
                                                      tick >= second.Tick ? second.Tick : first.Tick);
            const bool cPlace = !cOwnerLies && !m_actorBridge.IsLyingInRagdoll(pFormIdComponent->Id);
            if (!cPlace)
                ease.Held = true;
            else if (std::exchange(ease.Held, false))
            {
                glm::vec3 own{}, ownRotation{};
                m_actorBridge.GetActorPosition(pFormIdComponent->Id, own.x, own.y, own.z);
                m_actorBridge.GetActorRotation(pFormIdComponent->Id, ownRotation.x, ownRotation.y, ownRotation.z);
                ease.Begin(own - position, ownRotation.z - rotation.z, currentTick);
            }
            if (cPlace)
                ease.Apply(currentTick, position, rotation.z);
            // Stepped by its engine, so its capsule pushes what it meets, only while its owner is on the ground (from
            // take-off to landing and in any other state placed); placed when far off too (actor-movement.md).
            glm::vec3 at{};
            m_actorBridge.GetActorPosition(pFormIdComponent->Id, at.x, at.y, at.z);
            const bool cOwnerOnGround = tick >= second.Tick ? second.OnGround : (first.OnGround && second.OnGround);
            // Getting onto a mount its capsule collides with nothing, as its owner's clip carries it through the horse:
            // colliding, it shoved this game's own horse 60 units, and its owner's get-on ended beside it (the duo).
            const bool cGettingOn = ReplayState::GettingOnMount(m_actorBridge.GetActorMount(pFormIdComponent->Id) != 0,
                                                                m_actorBridge.GetActorSitState(pFormIdComponent->Id));
            const bool cOwnerCollides = !cGettingOn && (tick >= second.Tick ? second.CapsuleCollides : first.CapsuleCollides);
            const CopyMove cMove = CopyStep::Choose(!cPlace, cOwnerOnGround, cOwnerCollides, at, position);
            const float cPosition[3]{position.x, position.y, position.z};
            const float cRotation[3]{rotation.x, rotation.y, rotation.z};
            // Its capsule collides as its owner's: in furniture it does not, else physics pushes it out (actor-movement.md).
            m_actorBridge.SetCapsuleCollision(pFormIdComponent->Id, cOwnerCollides);
            // Its bodies and graphs as its owner's: seated, its bodies take the crosshair (re/systems/dialogue.md).
            if (!cOwnerLies)
            {
                const auto& cOwner = tick >= second.Tick ? second : first;
                m_actorBridge.SetCopyPhysics(pFormIdComponent->Id, cOwner.BodiesLayer, cOwner.GraphsStepPhysics);
            }
            // On its stream its graph's variables are the stream's alone.
            static const AnimationVariables s_none;
            if (m_world.GetGraphStreams().OnStream(pFormIdComponent->Id))
                pVariables = &s_none;
            m_actorBridge.ApplyRemoteMovement(pFormIdComponent->Id, cPosition, cRotation, second.Direction, *pVariables, cMove);
        }
    }

    auto animatedView = m_world.view<RemoteComponent, RemoteAnimationComponent, FormIdComponent>();

    for (auto entity : animatedView)
    {
        auto& formIdComponent = animatedView.get<FormIdComponent>(entity);

        AnimationSystem::Update(m_world, entity, formIdComponent.Id, animatedView.get<RemoteAnimationComponent>(entity),
                                InterpolationSystem::PlaybackTick(m_world, entity));
    }
}

void MovementService::OnReferenceJump(const ReferenceJumpEvent& acEvent) noexcept
{
    m_jumped.insert(acEvent.FormId);
}

