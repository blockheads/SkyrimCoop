#include <Bridges/PhysicsRests.h>

#include <Forms/FormChanges.h>
#include <TESObjectREFR.h>

#include <glm/glm.hpp>

#include <mutex>

namespace
{
// At rest: this close to where it was this many updates ago (a ladle on a slope creeps and rocks for 20 s).
constexpr uint32_t kRestUpdates = 15;
constexpr float kRestDistance = 0.5f;
constexpr float kRestAngle = 0.005f;

// The references the funnel saw physics move since the last update, filled on the engine's threads. A leaf lock.
std::mutex s_movedLock;
std::unordered_set<uint32_t> s_moved;

// The havok move flag: every frame while a body is awake, at rest too.
void OnFormChange(const TESForm& acForm, uint32_t aFlags, bool, bool aRecorded) noexcept
{
    if (!aRecorded || !(aFlags & TESObjectREFR::CHANGE_REFR_HAVOK_MOVE) || acForm.formType != FormType::Reference)
        return;
    std::scoped_lock _(s_movedLock);
    s_moved.insert(acForm.formID);
}
} // namespace

PhysicsRests::Pose PhysicsRests::Pose::Of(const TESObjectREFR& acObject) noexcept
{
    return {{acObject.position.x, acObject.position.y, acObject.position.z}, {acObject.rotation.x, acObject.rotation.y, acObject.rotation.z}};
}

bool PhysicsRests::Pose::IsNear(const Pose& acOther) const noexcept
{
    return glm::all(glm::lessThan(glm::abs(Position - acOther.Position), glm::vec3(kRestDistance))) &&
           glm::all(glm::lessThan(glm::abs(Rotation - acOther.Rotation), glm::vec3(kRestAngle)));
}

PhysicsRests::PhysicsRests() noexcept
{
    if (!FormChanges::Watch(&OnFormChange))
        spdlog::error("{}: no slot left to watch the change funnel, physics rests stay in this game", __FUNCTION__);
}

PhysicsRests::~PhysicsRests() noexcept
{
    FormChanges::Unwatch(&OnFormChange);
    std::scoped_lock _(s_movedLock);
    s_moved.clear();
}

void PhysicsRests::Moved(const TESObjectREFR& acObject) noexcept
{
    if (m_moving.count(acObject.formID))
        return;
    const Pose cNow = Pose::Of(acObject);
    // Still where it rested: the flag of a body not yet asleep.
    if (const auto it = m_rested.find(acObject.formID); it != m_rested.end() && it->second.IsNear(cNow))
        return;
    m_moving[acObject.formID] = {cNow, cNow, 0};
}

std::vector<uint32_t> PhysicsRests::TakeRested() noexcept
{
    std::unordered_set<uint32_t> moved;
    {
        std::scoped_lock _(s_movedLock);
        moved.swap(s_moved);
    }
    for (const uint32_t cFormId : moved)
        if (const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId)))
            Moved(*pObject);

    std::vector<uint32_t> rested;
    for (auto it = m_moving.begin(); it != m_moving.end();)
    {
        const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(it->first));
        if (!pObject || pObject->IsDeleted())
        {
            m_rested.erase(it->first);
            it = m_moving.erase(it);
            continue;
        }
        Watch& watch = it->second;
        const Pose cNow = Pose::Of(*pObject);
        if (!watch.Last.IsNear(cNow))
            watch = {watch.Start, cNow, 0};
        else if (++watch.Quiet >= kRestUpdates)
        {
            m_rested[it->first] = cNow;
            if (!watch.Start.IsNear(cNow))
                rested.push_back(it->first);
            it = m_moving.erase(it);
            continue;
        }
        ++it;
    }
    return rested;
}

void PhysicsRests::Forget(uint32_t aFormId) noexcept
{
    m_moving.erase(aFormId);
    m_rested.erase(aFormId);
}
