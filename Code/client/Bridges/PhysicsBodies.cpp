#include <Bridges/PhysicsBodies.h>

#include <Physics/BodyPrediction.h>

#include <glm/glm.hpp>

namespace
{
// The engine skipped its body to node write (it leaves the node exactly at the body otherwise). Writing a followed
// body's node again re-places it, losing its contacts: a dropped bucket sank 21 units into a head.
bool NodeLeftBehind(const IBodyReads::Body& acBody) noexcept
{
    if (!acBody.Node)
        return false;
    const auto cApart = BodyPrediction::Between({.Position = acBody.Node->first, .Rotation = acBody.Node->second},
                                                {.Position = acBody.Read.Position, .Rotation = acBody.Read.Rotation});
    return cApart.Distance > PhysicsBodies::kLeftBehind || cApart.Angle > PhysicsBodies::kLeftBehindAngle;
}

BodyFrame FrameOf(const BodyRead& acRead, uint8_t aIndex) noexcept
{
    return {.Key = {.Index = aIndex}, .Position = acRead.Position, .Rotation = acRead.Rotation, .LinearVelocity = acRead.LinearVelocity,
            .AngularVelocity = acRead.AngularVelocity};
}
} // namespace

PhysicsBodies::Pose PhysicsBodies::Pose::Of(const IBodyReads::Reference& acReference) noexcept
{
    return {acReference.Position, acReference.Rotation};
}

bool PhysicsBodies::Pose::IsNear(const Pose& acOther) const noexcept
{
    return glm::all(glm::lessThan(glm::abs(Position - acOther.Position), glm::vec3(kBodySameDistance))) &&
           glm::all(glm::lessThan(glm::abs(Rotation - acOther.Rotation), glm::vec3(kBodySameAngle)));
}

PhysicsBodies::PhysicsBodies(IBodyReads& aReads, bool aAllActors) noexcept
    : m_reads(aReads)
    , m_allActors(aAllActors)
{
}

PhysicsBodies::Update PhysicsBodies::Take() noexcept
{
    auto moved = m_reads.TakeMovedReferences();
    // A revived player's next ragdoll need not mark the reference as moved again.
    const uint32_t cPlayerRagdoll = m_reads.PlayerInRagdoll();
    if (cPlayerRagdoll && !m_playerRagdoll)
        moved.push_back(cPlayerRagdoll);
    m_playerRagdoll = cPlayerRagdoll != 0;
    for (const uint32_t cFormId : moved)
        if (const auto cReference = m_reads.ReadReference(cFormId, false); cReference && (m_allActors || !cReference->Actor || cReference->Player))
            m_awake.try_emplace(cFormId, Moving{Pose::Of(*cReference)});

    // Held still (pressed against something) Havok puts it to sleep, but a hand holds it up: not a rest.
    const uint32_t cHeld = m_reads.PlayerHolds();
    Update update;
    for (auto it = m_awake.begin(); it != m_awake.end();)
    {
        const auto cReference = m_reads.ReadReference(it->first);
        if (!cReference)
        {
            it = m_awake.erase(it);
            continue;
        }
        const bool cActor = cReference->Actor;
        // A living actor's ragdoll is its physics' while it lies and while it gets up; standing, its animation's.
        const bool cUp = cReference->Up;
        Bodies awake{it->first, cActor};
        Bodies all{it->first, cActor};
        for (size_t i = 0; i < cReference->Bodies.size(); ++i)
        {
            const auto& cBody = cReference->Bodies[i];
            if (!cBody || (!cBody->Read.Dynamic && !cUp))
                continue;
            const BodyRead& cRead = cBody->Read;
            // Up again: where its bodies are is its rest (read once, below).
            if (cUp)
            {
                all.Read.push_back(FrameOf(cRead, static_cast<uint8_t>(i)));
                continue;
            }
            // The engine sometimes leaves a flying body's 3D (and reference) where it was (docs/re/systems/physics.md);
            // the guests and the record get where it is.
            if (!cActor && NodeLeftBehind(*cBody))
                QueueNode(it->first, static_cast<uint8_t>(i));
            BodyFrame frame = FrameOf(cRead, static_cast<uint8_t>(i));
            // Only a lying owner's frame takes a standing copy down: a getting-up owner's can come late.
            frame.Knocked = cActor && !cReference->Dead && !cReference->Rising;
            // Getting up its animation drives it, and its limbs shake step to step: a pose to blend between, no
            // flight to guess onward (a copy's hand shook twice as far as its owner's).
            if (cActor && cReference->Rising)
                frame.LinearVelocity = frame.AngularVelocity = {};
            all.Read.push_back(frame);
            if (cRead.Active || it->first == cHeld)
                awake.Read.push_back(frame);
        }
        if (!awake.Read.empty())
        {
            it->second.Ragdoll |= cActor;
            it->second.Resting = false;
            if (cActor)
                it->second.Last = all.Read;
            update.Awake.push_back(std::move(awake));
            ++it;
            continue;
        }
        if (cActor && !cUp && all.Read.empty())
        {
            ++it;
            continue;
        }
        // A living actor lying still: its rest once, and kept until it stands (its get-up moves its bodies again,
        // and need not mark it moved).
        if (cActor && !cUp && !cReference->Dead)
        {
            if (!std::exchange(it->second.Resting, true))
                update.Rested.push_back(std::move(all));
            ++it;
            continue;
        }
        // Standing, its bodies have left the world: its last frame is its rest.
        if (cUp && it->second.Ragdoll && all.Read.empty())
            for (BodyFrame frame : it->second.Last)
            {
                frame.Knocked = false;
                all.Read.push_back(frame);
            }
        // Asleep, or an actor up again from lying (an actor standing never lay): where it rests is the owner's.
        if (!all.Read.empty() && (!cUp || it->second.Ragdoll))
        {
            // An actor's place is its own sync's; a reference's move row follows its queued node repair.
            if (!cActor)
                QueueRest(it->first, it->second.At);
            update.Rested.push_back(std::move(all));
        }
        it = m_awake.erase(it);
    }
    return update;
}

std::vector<uint32_t> PhysicsBodies::TakeRested() noexcept
{
    return std::exchange(m_restedAway, {});
}

void PhysicsBodies::Forget(uint32_t aFormId) noexcept
{
    ++m_sceneGeneration[aFormId];
    m_awake.erase(aFormId);
    std::erase(m_restedAway, aFormId);
}

void PhysicsBodies::QueueNode(uint32_t aFormId, uint8_t aIndex) noexcept
{
    m_mainTasks.Queue([this, aFormId, aIndex, generation = m_sceneGeneration[aFormId]] {
        if (generation != m_sceneGeneration[aFormId])
            return;
        const auto cReference = m_reads.ReadReference(aFormId);
        if (!cReference || cReference->Actor || aIndex >= cReference->Bodies.size())
            return;
        const auto& cBody = cReference->Bodies[aIndex];
        // The engine may have followed it itself since the snapshot: keep those contacts.
        if (cBody && NodeLeftBehind(*cBody))
            m_reads.WriteNode(aFormId, aIndex);
    });
}

void PhysicsBodies::QueueRest(uint32_t aFormId, const Pose& acStart) noexcept
{
    m_mainTasks.Queue([this, aFormId, start = acStart, generation = m_sceneGeneration[aFormId]] {
        if (generation != m_sceneGeneration[aFormId])
            return;
        const auto cNow = m_reads.ReadReference(aFormId, false);
        if (cNow && !start.IsNear(Pose::Of(*cNow)))
            m_restedAway.push_back(aFormId);
    });
}
