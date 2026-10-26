#include <Bridges/PhysicsBodies.h>
#include <Bridges/ReferenceBodies.h>

#include <Actor.h>
#include <Forms/FormChanges.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <NetImmerse/NiNode.h>
#include <TESObjectREFR.h>

#include <glm/glm.hpp>

#include <mutex>
#include <RTTI.h>

namespace
{

// The references the funnel saw physics move since the last update, filled on the engine's threads. A leaf lock.
std::mutex s_movedLock;
std::unordered_set<uint32_t> s_moved;

// The havok move flag: every frame while a body is awake.
void OnFormChange(const TESForm& acForm, uint32_t aFlags, bool, bool aRecorded) noexcept
{
    if (!aRecorded || !(aFlags & TESObjectREFR::CHANGE_REFR_HAVOK_MOVE) ||
        (acForm.formType != FormType::Reference && acForm.formType != FormType::Character))
        return;
    std::scoped_lock _(s_movedLock);
    s_moved.insert(acForm.formID);
}
} // namespace

PhysicsBodies::Pose PhysicsBodies::Pose::Of(const TESObjectREFR& acObject) noexcept
{
    return {{acObject.position.x, acObject.position.y, acObject.position.z}, {acObject.rotation.x, acObject.rotation.y, acObject.rotation.z}};
}

bool PhysicsBodies::Pose::IsNear(const Pose& acOther) const noexcept
{
    return glm::all(glm::lessThan(glm::abs(Position - acOther.Position), glm::vec3(ReferenceBodies::kSameDistance))) &&
           glm::all(glm::lessThan(glm::abs(Rotation - acOther.Rotation), glm::vec3(ReferenceBodies::kSameAngle)));
}

PhysicsBodies::PhysicsBodies(bool aActors) noexcept
    : m_actors(aActors)
{
    if (!FormChanges::Watch(&OnFormChange))
        spdlog::error("{}: no slot left to watch the change funnel, physics stays in this game", __FUNCTION__);
}

PhysicsBodies::~PhysicsBodies() noexcept
{
    FormChanges::Unwatch(&OnFormChange);
    std::scoped_lock _(s_movedLock);
    s_moved.clear();
}

PhysicsBodies::Update PhysicsBodies::Take() noexcept
{
    std::unordered_set<uint32_t> moved;
    {
        std::scoped_lock _(s_movedLock);
        moved.swap(s_moved);
    }
    for (const uint32_t cFormId : moved)
        if (const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId)); pObject && (m_actors || !Cast<Actor>(pObject)))
            m_awake.try_emplace(cFormId, Pose::Of(*pObject));

    Update update;
    for (auto it = m_awake.begin(); it != m_awake.end();)
    {
        auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(it->first));
        NiNode* p3D = pObject && !pObject->IsDeleted() ? pObject->GetNiNode() : nullptr;
        if (!p3D)
        {
            it = m_awake.erase(it);
            continue;
        }
        const bool cActor = Cast<Actor>(pObject) != nullptr;
        Bodies awake{it->first, cActor};
        Bodies all{it->first, cActor};
        // A reference's: with their collision objects, whose nodes this game writes (below).
        const auto cCollisions = cActor ? std::vector<bhkNiCollisionObject*>{} : CollisionObjectsOf(*p3D);
        std::vector<bhkWorldObject*> bodies;
        if (cActor)
            bodies = RagdollBodiesOf(*pObject);
        else
            for (bhkNiCollisionObject* pCollision : cCollisions)
                bodies.push_back(pCollision->body);
        const auto& cBodies = bodies;
        for (size_t i = 0; i < cBodies.size(); ++i)
        {
            BodyRead read;
            if (!cBodies[i] || !ReadBody(*cBodies[i], read) || !read.Dynamic)
                continue;
            // The engine sometimes leaves a flying body's 3D (and reference) where it was (docs/re/systems/physics.md);
            // the guests and the record get where it is.
            if (!cActor)
                FollowBody(*cCollisions[i]);
            const BodyFrame cFrame = ReferenceBodies::FrameOf(read, static_cast<uint8_t>(i));
            all.Read.push_back(cFrame);
            if (read.Active)
                awake.Read.push_back(cFrame);
        }
        if (!awake.Read.empty())
        {
            update.Awake.push_back(std::move(awake));
            ++it;
            continue;
        }
        // Asleep: where it rests is the host's.
        if (!all.Read.empty())
        {
            // An actor's place is its own sync's (movement, the copy contract).
            if (!cActor && !it->second.IsNear(Pose::Of(*pObject)))
                m_restedAway.push_back(it->first);
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
    m_awake.erase(aFormId);
    std::erase(m_restedAway, aFormId);
}
