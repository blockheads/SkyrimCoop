#pragma once

#include <physics/IBodyBridge.h>
#include <physics/BodyGroups.h>

#include <physics/BodyPrediction.h>

#include <chrono>
#include <map>
#include <set>

struct Actor;
struct bhkWorldObject;
struct bhkNiCollisionObject;

class BodyBridge final : public IBodyBridge
{
public:
    void SetReferences(std::function<uint32_t(const GameId& acId)> aFormIdOf, std::function<std::optional<GameId>(uint32_t aFormId)> aIdOf) noexcept override;
    [[nodiscard]] std::optional<BodyRead> Read(const BodyKey& acKey) noexcept override;
    [[nodiscard]] std::vector<std::pair<BodyKey, BodyKey>> ReadTouching(const std::vector<BodyKey>& acKeys) noexcept override;
    bool Drive(const BodyFrame& acFrame, uint32_t aCopyFormId = 0, bool aHeld = false) noexcept override;
    bool Glide(const BodyPose& acRest) noexcept override;
    bool Place(const BodyPose& acRest) noexcept override;
    bool Sleep(const BodyKey& acKey) noexcept override;
    bool HoldAt(const BodyPose& acRest) noexcept override;
    bool RestRagdoll(const BodyPose& acRest, uint32_t aCopyFormId) noexcept override;
    bool Release(const BodyKey& acKey) noexcept override;
    [[nodiscard]] uint32_t ReferenceFormId(const BodyKey& acKey) const noexcept override;
    void SetCopies(const std::vector<uint32_t>& acCopyFormIds) noexcept override;
    bool KnockDown(uint32_t aCopyFormId) noexcept override;
    void SetMoving(const BodyKey& acKey, bool aMoving) noexcept override;
    void Forget(const BodyKey& acKey) noexcept override;
    void ForgetAll() noexcept override;
    bool KeepMotion(const BodyKey& acKey) noexcept override;
    bool Settle(const BodyKey& acKey) noexcept override;

private:
    // A body here: a reference's by its 3D's order (with its collision object, whose node shows it), a copy's
    // ragdoll's by the ragdoll's; none for a copy's body its animation still drives (Waiting).
    struct Found
    {
        bhkWorldObject* pBody{};
        bhkNiCollisionObject* pCollision{};
        bool Waiting{};
        bool First{};
    };
    // aKnock: a knocked down owner's frame, which knocks a living copy down (a dead owner's, a rest do not).
    [[nodiscard]] Found Find(const BodyKey& acKey, uint32_t aCopyFormId, bool aKnock) noexcept;
    // A living copy lies in its ragdoll while its owner's simulates: knocked down with no force at the owner's first
    // frame (AIProcess::KnockExplosion 39895), until it is up again.
    [[nodiscard]] bool KnockedDown(Actor& aCopy, bool aKnock) noexcept;
    // How a reference's body moved before it is first held, to give it back.
    void RememberMotion(const BodyKey& acKey, const Found& acBody) noexcept;
    bool SettleFound(const BodyKey& acKey, const Found& acBody) noexcept;

    // Its class set on its body (its collision group): the group it had is kept to give back.
    void SetClass(const BodyKey& acKey, bhkWorldObject* apBody, BodyClass aClass) noexcept;
    // Whether anything here is played (a reference's body or a copy's lying ragdoll).
    [[nodiscard]] bool AnyPlayed() const noexcept;
    // The first played body is here: every moving body waiting for its group gets it.
    void WriteMoving() noexcept;
    struct Class
    {
        BodyClass Is{};
        uint16_t Group{}; // its own, before ours
        // Our group written on its body. A moving body's waits for the first played one: only that pair needs it, and
        // each write makes Havok redo the body's agents (77794), which drops its contacts for a step.
        bool Applied = true;
        uint16_t Moving{}; // the moving group it holds (MovingGroups::Take), given back when it stops moving
    };
    std::map<BodyKey, Class> m_classes;
    BodyGroups::MovingGroups m_movingGroups;
    std::map<BodyKey, uint16_t> m_ragdollMoving; // a copy's corpse this game's player took: its moving group
    std::function<uint32_t(const GameId& acId)> m_formIdOf;
    std::function<std::optional<GameId>(uint32_t aFormId)> m_idOf;
    std::map<BodyKey, uint8_t> m_motions; // the reference bodies held here: their own motion type
    // Another game's rest being put here: its wakes are the rest's, not this game moving it (SetMoving).
    std::set<BodyKey> m_placing;
    // A held reference body keyframed for its hold here first gets its own motion back (Glide, Place).
    bool Unhold(const BodyKey& acKey, const Found& acBody) noexcept;
    std::map<BodyKey, std::pair<uint32_t, bhkWorldObject*>> m_ragdolls; // copy and body generation initialized here
    // A lying copy's ragdoll bodies are played (BodyGroups): each one's own group, given back as it stands.
    std::map<BodyKey, uint16_t> m_ragdollGroups;
    void StandRagdoll(Actor& aCopy, const BodyKey& acKey) noexcept;
    std::map<uint32_t, bool> m_knocked;   // living copies knocked down with their owners: lying yet
    std::chrono::steady_clock::time_point m_lastFrame{}; // the last frame that drove a body
    float m_frameSeconds = BodyPrediction::kSteerSeconds;  // the time between those frames (SteerSeconds)
};
