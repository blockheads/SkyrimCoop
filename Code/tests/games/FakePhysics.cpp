#include <Events/MenuEvent.h>
#include <Games/CheckedPlacement.h>
#include <Events/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include "FakeDialogue.h"
#include <Events/PlayerTravelQueuedEvent.h>
#include <Events/StoryEvents.h>

#include <Events/ContainerChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/AddTargetEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Games/DeathActions.h>
#include <Events/HitEvent.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Events/SpellCastEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/LooseItemEvents.h>
#include <Events/CallEndedEvent.h>
#include <Events/RoutedCallEvent.h>
#include <Events/PlayerProgressEvent.h>
#include <Games/EngineNames.h>
#include <Events/RemoveWaypointEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <Papyrus/PartyNatives.h>
#include <Papyrus/SystemQuests.h>
#include <Events/SystemQuestLearnedEvent.h>
#include <ScriptValues.h>
#include <array>
#include <ranges>
#include <cstdio>
#include <Games/Overrides.h>
#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Services/SaveService.h>
#include <Wire/Buffer.h>
#include <World.h>
#include <FormIds.h>

using namespace PartyRules;

float FakeEngine::HeadingOf(const glm::quat& acRotation) noexcept
{
    const glm::vec3 cForward = acRotation * glm::vec3(0.f, 1.f, 0.f);
    const float cHeading = std::atan2(cForward.x, cForward.y);
    return cHeading < 0.f ? cHeading + 6.28318531f : cHeading;
}

void FakeEngine::Knock(const BodyKey& acKey, const glm::vec3& acVelocity, const glm::vec3& acSpin)
{
    Body& body = Bodies.at(acKey);
    if (body.RagdollOf && Actors.contains(body.RagdollOf) && !IsActorDead(body.RagdollOf))
        Actors.at(body.RagdollOf).KnockedDown = true;
    body.Flight.Resting = false;
    body.Flight.Body.LinearVelocity = acVelocity;
    body.Flight.Body.AngularVelocity = acSpin;
    body.Awake = true;
    body.Tick = 0;
}

std::optional<BodyPose> FakeEngine::ReadPose(const BodyKey& acKey) noexcept
{
    const auto it = acKey.Actor ? Bodies.end() : Bodies.find(acKey);
    if (it == Bodies.end())
        return std::nullopt;
    return BodyPose{.Key = acKey, .Position = it->second.Flight.Body.Position, .Rotation = it->second.Flight.Body.Rotation};
}

bool FakeEngine::Push(const BodyPush& acPush) noexcept
{
    const auto it = Bodies.find(acPush.Key);
    if (it == Bodies.end() || it->second.Held)
        return false;
    Knock(acPush.Key, it->second.Flight.Body.LinearVelocity + acPush.Impulse, it->second.Flight.Body.AngularVelocity);
    return true;
}

bool FakeEngine::Drive(const BodyFrame& acFrame, uint32_t aCopyFormId) noexcept
{
    const auto it = Bodies.find(acFrame.Key);
    // An actor's body needs its copy here (BodyBridge::Find): none yet, or gone, is not loaded here.
    if (it == Bodies.end() || (acFrame.Key.Actor && !Actors.contains(aCopyFormId)))
        return false;
    // A living copy's ragdoll body is its animation's: knocked down with its owner's, else left until its own death
    // (BodyBridge::Find).
    if (acFrame.Key.Actor && !IsActorDead(aCopyFormId))
    {
        auto& copy = Actors.at(aCopyFormId);
        if (!acFrame.Knocked || !copy.KnockedDown)
        {
            if (acFrame.Knocked && !copy.KnockIn)
                copy.KnockIn = 2;
            return true;
        }
    }
    if (it->second.NeedsKnock && !Actors.at(aCopyFormId).KnockedDown)
        return true;
    if (acFrame.Key.Actor)
    {
        m_pendingRagdolls.Put(aCopyFormId, acFrame, false);
        return true;
    }
    Body& body = it->second;
    body.Held = true;
    body.Awake = false;
    body.Flight.Body.Position = acFrame.Position;
    body.Flight.Body.Rotation = acFrame.Rotation;
    ++body.Drives;
    return true;
}

bool FakeEngine::Rest(const BodyPose& acPose, uint32_t aCopyFormId) noexcept
{
    const auto it = Bodies.find(acPose.Key);
    if (it == Bodies.end() || (acPose.Key.Actor && !Actors.contains(aCopyFormId)))
        return false;
    ++it->second.RestAttempts;
    if (it->second.RestBusy)
        return false;
    if (acPose.Key.Actor)
    {
        if (IsActorDead(aCopyFormId) || Actors.at(aCopyFormId).KnockedDown)
        {
            ++it->second.Rests;
            m_pendingRagdolls.Put(aCopyFormId, {.Key = acPose.Key, .Position = acPose.Position, .Rotation = acPose.Rotation,
                                               .Knocked = acPose.Knocked}, true);
        }
        return true;
    }
    Body& body = it->second;
    // A reference's body is simulated again at its rest; a ragdoll's stays held.
    body.Held = body.RagdollOf != 0;
    body.Flight.Body.Position = acPose.Position;
    body.Flight.Body.Rotation = acPose.Rotation;
    body.Flight.Body.LinearVelocity = {};
    body.Flight.Body.AngularVelocity = {};
    ++body.Rests;
    return true;
}

bool FakeEngine::Free(const BodyFrame& acFrame) noexcept
{
    const auto it = Bodies.find(acFrame.Key);
    if (it == Bodies.end())
        return false;
    Body& body = it->second;
    body.Held = false;
    body.Flight.Body = acFrame;
    body.Flight.Body.Key = {};
    Knock(acFrame.Key, acFrame.LinearVelocity, acFrame.AngularVelocity);
    return true;
}

HostBodies FakeEngine::TakeBodies(uint64_t aTick, const std::function<bool(const BodyKey&)>& acWhich)
{
    constexpr uint64_t cStepMs = 16; // Havok's substeps (fTimePerSubStep)
    HostBodies bodies;
    for (auto& [cKey, body] : Bodies)
    {
        if (!body.Awake || (acWhich && !acWhich(cKey)))
            continue;
        if (!body.Tick)
            body.Tick = aTick;
        for (; body.Tick + cStepMs <= aTick && !body.Flight.Resting; body.Tick += cStepMs)
            body.Flight.Step(cStepMs / 1000.f);
        BodyFrame frame = body.Flight.Body;
        frame.Key = cKey;
        // Skyrim keeps a ragdolled actor on its ragdoll's root body, turned with it (Actor's BSTransformDeltaEvent sink,
        // 37996).
        if (body.RagdollOf && cKey.Index == 0)
            if (const auto it = Actors.find(body.RagdollOf); it != Actors.end())
            {
                std::copy_n(&frame.Position.x, 3, it->second.Position);
                it->second.Rotation[2] = HeadingOf(frame.Rotation);
            }
        // A living actor's ragdoll simulated: knocked down (PhysicsBodies).
        frame.Knocked = body.RagdollOf && !IsActorDead(body.RagdollOf);
        if (body.Flight.Resting)
        {
            body.Awake = false;
            bodies.Rested.push_back({cKey, frame.Position, frame.Rotation, frame.Knocked});
        }
        else
            bodies.Awake.push_back(frame);
    }
    return bodies;
}

