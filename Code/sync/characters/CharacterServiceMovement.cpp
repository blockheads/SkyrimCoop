// CharacterService: movement snapshots, graph streams and each frame's updates.
#include <characters/CharacterService.h>
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

void CharacterService::OnReferencesMoveRequest(const NotifyReferencesMove& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent>();
    auto& clock = m_world.GetCopyClock();
    clock.OnSnapshot(acMessage.Tick, m_transport.GetTick());

    for (const auto& [sessionId, update] : acMessage.Updates)
    {
        auto itor = std::find_if(std::begin(view), std::end(view), [sessionId = sessionId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == sessionId; });

        if (itor == std::end(view))
            continue;

        auto& interpolationComponent = view.get<InterpolationComponent>(*itor);
        auto& animationComponent = view.get<RemoteAnimationComponent>(*itor);
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
            auto& place = m_world.get_or_emplace<OwnerPlaceComponent>(*itor);
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

void CharacterService::OnStreamedRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept
{
    if (const auto* pForm = aRegistry.try_get<FormIdComponent>(aEntity))
        m_world.GetGraphStreams().Forget(pForm->Id);
}

void CharacterService::OnGraphOps(const NotifyGraphOps& acMessage) const noexcept
{
    auto& streams = m_world.GetGraphStreams();
    auto view = m_world.view<RemoteComponent, FormIdComponent>();
    for (const auto& cRun : acMessage.Ops.Runs)
        for (const auto cEntity : view)
            if (view.get<RemoteComponent>(cEntity).Id == cRun.ActorId)
            {
                const uint32_t cFormId = view.get<FormIdComponent>(cEntity).Id;
                // A copy that died off its stream (a pair with this game's actor) stays off it.
                if (m_actorBridge.IsActorDead(cFormId) && !streams.OnStream(cFormId))
                    break;
                streams.Feed(cFormId, cRun, acMessage.Ops.Frames);
                if (streams.ShouldAsk(cFormId))
                {
                    GraphSyncRequest ask;
                    ask.Id = cRun.ActorId;
                    m_transport.Send(ask);
                }
                break;
            }
}

void CharacterService::OnGraphSync(const NotifyGraphSync& acMessage) const noexcept
{
    auto view = m_world.view<LocalComponent, FormIdComponent>();
    for (const auto cEntity : view)
        if (view.get<LocalComponent>(cEntity).Id == acMessage.Id)
            m_world.GetGraphStreams().WantPoint(view.get<FormIdComponent>(cEntity).Id);
}

void CharacterService::OnGraphSyncPoint(const NotifyGraphSyncPoint& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent, FormIdComponent>();
    for (const auto cEntity : view)
        if (view.get<RemoteComponent>(cEntity).Id == acMessage.Id)
        {
            spdlog::info("Copy {:X} starts on its owner's graph stream at update {} ({} clip sets held)", view.get<FormIdComponent>(cEntity).Id, acMessage.Update, acMessage.ClipSets.size());
            m_world.GetGraphStreams().Load(view.get<FormIdComponent>(cEntity).Id, acMessage);
        }
}

void CharacterService::SendGraphStream() const noexcept
{
    auto& streams = m_world.GetGraphStreams();
    if (auto ops = streams.TakeOps())
    {
        GraphOpsRequest request;
        request.Ops = std::move(*ops);
        m_transport.Send(request);
    }
    for (auto& point : streams.TakePoints())
    {
        GraphSyncPointRequest request;
        static_cast<Payloads::GraphSyncPoint&>(request) = std::move(point);
        m_transport.Send(request);
    }
}

void CharacterService::OnActionEvent(const ActionEvent& acActionEvent) const noexcept
{
    auto view = m_world.view<LocalAnimationComponent, FormIdComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acActionEvent.ActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (itor != std::end(view))
    {
        auto& localComponent = view.get<LocalAnimationComponent>(*itor);

        ActionEvent action = acActionEvent;
        AnimationSystem::TargetToNetwork(action);
        localComponent.Append(action);
    }
}

void CharacterService::PlaceAt(uint32_t aFormId, const ActorWhereabouts& acWhereabouts) const noexcept
{
    if (acWhereabouts.Where != ActorWhereabouts::State::kPlaced)
        return;
    if (const uint32_t cCellId = m_world.GetTravelService().CellAt(acWhereabouts.WorldSpaceId, acWhereabouts.CellId, acWhereabouts.Position))
    {
        m_actorBridge.MoveActorTo(aFormId, cCellId, acWhereabouts.Position.x, acWhereabouts.Position.y, acWhereabouts.Position.z);
        m_actorBridge.SetActorRotation(aFormId, 0.f, acWhereabouts.Heading);
    }
}

void CharacterService::FollowOwner(entt::entity aEntity, uint32_t aFormId, const ActorWhereabouts& acWhereabouts, bool aOwnerGone) const noexcept
{
    // Still a copy: a guest never runs a host NPC (copy-contract.md, "When the owner's actor goes away").
    m_world.emplace_or_replace<HeldCopyComponent>(aEntity);
    // Before the move: kept and hidden, the engine neither loads nor frees it where it goes (copy-lifecycle.md).
    if (aOwnerGone)
        m_dispatcher.trigger(CopyHeldEvent{aEntity, aFormId,
                                           acWhereabouts.Where == ActorWhereabouts::State::kDisabled ||
                                               acWhereabouts.Where == ActorWhereabouts::State::kDeleted});
    switch (acWhereabouts.Where)
    {
    case ActorWhereabouts::State::kPlaced:
    {
        const uint32_t cCellId = m_world.GetTravelService().CellAt(acWhereabouts.WorldSpaceId, acWhereabouts.CellId, acWhereabouts.Position);
        if (!cCellId)
        {
            spdlog::warn("Copy {:X}: its owner's actor went to cell {:X} (worldspace {:X}), which this game lacks", aFormId,
                         acWhereabouts.CellId.BaseId, acWhereabouts.WorldSpaceId.BaseId);
            return;
        }
        float x = 0.f, y = 0.f, z = 0.f;
        m_actorBridge.GetActorPosition(aFormId, x, y, z);
        const glm::vec3 cHere{x, y, z};
        if (m_actorBridge.GetActorParentCellFormId(aFormId) != cCellId || glm::distance(cHere, glm::vec3(acWhereabouts.Position)) >= 64.f)
        {
            spdlog::info("Copy {:X} follows its owner's actor to cell {:X}", aFormId, cCellId);
            m_actorBridge.MoveActorTo(aFormId, cCellId, acWhereabouts.Position.x, acWhereabouts.Position.y, acWhereabouts.Position.z);
            m_actorBridge.SetActorRotation(aFormId, 0.f, acWhereabouts.Heading);
        }
        return;
    }
    case ActorWhereabouts::State::kDisabled:
    case ActorWhereabouts::State::kDeleted:
        // Disabled only: a guest deleting a plugin's reference crashed its next load (2026-10-30-deleted-placed-ref-load.md);
        // the host's save carries the deletion.
        spdlog::info("Copy {:X}: its owner's actor was {}", aFormId,
                     acWhereabouts.Where == ActorWhereabouts::State::kDeleted ? "deleted" : "disabled");
        m_actorBridge.DisableActor(aFormId, false);
        return;
    default:
        return;
    }
}

ActorWhereabouts CharacterService::ReadWhereabouts(uint32_t aFormId) const noexcept
{
    ActorWhereabouts where;
    if (!m_actorBridge.ActorExists(aFormId))
        return where;
    if (m_actorBridge.IsActorDeleted(aFormId))
        where.Where = ActorWhereabouts::State::kDeleted;
    else if (m_actorBridge.IsActorDisabled(aFormId))
        where.Where = ActorWhereabouts::State::kDisabled;
    else
    {
        const uint32_t cCellId = m_actorBridge.GetActorParentCellFormId(aFormId);
        const uint32_t cWorldSpaceId = m_actorBridge.GetActorWorldSpaceFormId(aFormId);
        // A persistent reference outdoors out of the loaded cells has no parent cell, only its worldspace.
        if (!cCellId && !cWorldSpaceId)
            return {};
        const auto cCell = cCellId ? ToSessionCell(cCellId, cWorldSpaceId) : std::optional(GameId{});
        const auto cWorldSpace = cWorldSpaceId ? ToSession(cWorldSpaceId) : std::optional(GameId{});
        if (!cCell || !cWorldSpace)
            return {};
        where.CellId = *cCell;
        where.WorldSpaceId = *cWorldSpace;
        float x = 0.f, y = 0.f, z = 0.f;
        m_actorBridge.GetActorPosition(aFormId, x, y, z);
        where.Position = {x, y, z};
        m_actorBridge.GetActorRotation(aFormId, x, y, z);
        where.Heading = z;
        where.Where = ActorWhereabouts::State::kPlaced;
    }
    return where;
}

void CharacterService::MoveActor(uint32_t aFormId, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept
{
    m_mainTasks.Queue([this, aFormId, acWorldSpaceId, acCellId, acPosition] {
        if (!m_transport.IsOnline() || !m_actorBridge.ActorExists(aFormId))
            return;
        const uint32_t cCellFormId = m_world.GetTravelService().CellAt(acWorldSpaceId, acCellId, acPosition);
        if (!cCellFormId)
        {
            spdlog::error("MoveActor: failed to fetch cell to teleport, actor: {:X}, worldspace: {:X}, cell: {:X}, position: {}, {}, {}", aFormId, acWorldSpaceId.BaseId, acCellId.BaseId, acPosition.x, acPosition.y, acPosition.z);
            return;
        }

        m_actorBridge.MoveActorTo(aFormId, cCellFormId, acPosition.x, acPosition.y, acPosition.z);
    });
}

void CharacterService::RunLocalUpdates() noexcept
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
        const bool cWatched = cFormId == cPlayer || cFormId == m_riddenMount || m_actorBridge.GetActorAiFields(cFormId).CombatState != 0;
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
    SendGraphStream();
}

// Until its owner's snapshots say where it goes: at its owner's last place once in its cell, else where it is
// (RunPresence moves it), no goal to step to (actor-movement.md, "A goal outlives its place").
void CharacterService::HoldCopy(entt::entity aEntity, uint32_t aFormId) const noexcept
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

void CharacterService::RunRemoteUpdates() noexcept
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


    auto waitingView = m_world.view<FormIdComponent, WaitingFor3D>();

    std::vector<entt::entity> toRemove;
    for (auto entity : waitingView)
    {
        auto& formIdComponent = waitingView.get<FormIdComponent>(entity);
        auto& waitingFor3D = waitingView.get<WaitingFor3D>(entity);

        if (!m_actorBridge.ActorHas3D(formIdComponent.Id))
            continue;

        m_actorBridge.ActorLoadAnimationVariables(formIdComponent.Id, waitingFor3D.SpawnRequest.LatestAction.Variables);

        const bool cIsDead = m_actorBridge.IsActorDead(formIdComponent.Id);
        if (cIsDead != waitingFor3D.SpawnRequest.IsDead)
            waitingFor3D.SpawnRequest.IsDead ? m_actorBridge.KillActor(formIdComponent.Id) : m_actorBridge.RespawnActor(formIdComponent.Id);

        toRemove.push_back(entity);
        if (const auto* pCopy = m_world.try_get<RemoteComponent>(entity))
            SessionIdHere(pCopy->Id);

        // After its owner's items (its record's part), as after the spawn's inventory before.
        if (m_actorBridge.IsActorVampireLord(formIdComponent.Id))
            m_actorBridge.ActorFixVampireLordModel(formIdComponent.Id);

        spdlog::info("Applied 3D for actor, form id: {:X}", formIdComponent.Id);
        m_dispatcher.trigger(Copy3DEvent{formIdComponent.Id});
    }

    for (auto entity : toRemove)
        m_world.remove<WaitingFor3D>(entity);
}

void CharacterService::OnReferenceJump(const ReferenceJumpEvent& acEvent) noexcept
{
    m_jumped.insert(acEvent.FormId);
}
