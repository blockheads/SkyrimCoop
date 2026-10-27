#pragma once

#include <Bridges/IBodyBridge.h>

#include <map>
#include <set>

struct Actor;
struct bhkWorldObject;
struct bhkNiCollisionObject;

class BodyBridge final : public IBodyBridge
{
public:
    void SetReferences(std::function<uint32_t(const GameId& acId)> aFormIdOf) noexcept override;
    [[nodiscard]] std::optional<BodyPose> ReadPose(const BodyKey& acKey) noexcept override;
    bool Drive(const BodyFrame& acFrame, uint32_t aCopyFormId = 0) noexcept override;
    bool Rest(const BodyPose& acPose, uint32_t aCopyFormId = 0) noexcept override;
    bool Free(const BodyFrame& acFrame) noexcept override;
    bool Push(const BodyPush& acPush) noexcept override;
    bool KeepMotion(const BodyKey& acKey) noexcept override;
    bool Settle(const BodyKey& acKey) noexcept override;

private:
    // Units between a copy's ragdoll body and its frame past which it is placed there rather than steered.
    static constexpr float kSteerDistance = 64.f;

    // A body here: a reference's by its 3D's order (with its collision object, whose node shows it), a copy's
    // ragdoll's by the ragdoll's; none for a copy's body its animation still drives (Waiting).
    struct Found
    {
        bhkWorldObject* pBody{};
        bhkNiCollisionObject* pCollision{};
        bool Waiting{};
    };
    // aKnock: a knocked down owner's frame, which knocks a living copy down (a dead owner's, a rest do not).
    [[nodiscard]] Found Find(const BodyKey& acKey, uint32_t aCopyFormId, bool aKnock) noexcept;
    // A living copy lies in its ragdoll while its owner's simulates: knocked down with no force at the owner's first
    // frame (AIProcess::KnockExplosion 39895), until it is up again.
    [[nodiscard]] bool KnockedDown(Actor& aCopy, bool aKnock) noexcept;
    // How a reference's body moved before it is first held, to give it back.
    void RememberMotion(const BodyKey& acKey, const Found& acBody) noexcept;
    bool SettleFound(const BodyKey& acKey, const Found& acBody) noexcept;

    std::function<uint32_t(const GameId& acId)> m_formIdOf;
    std::map<BodyKey, uint8_t> m_motions; // the reference bodies held here: their own motion type
    std::set<BodyKey> m_ragdolls;         // copies' ragdoll bodies taken from their own ragdoll
    std::map<uint32_t, bool> m_knocked;   // living copies knocked down with their owners: lying yet
};
