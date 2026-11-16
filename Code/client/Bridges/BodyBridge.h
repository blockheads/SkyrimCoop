#pragma once

#include <Bridges/IBodyBridge.h>

#include <Physics/BodyPrediction.h>

#include <chrono>
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
    // A played reference body this far off its frame is put there, not steered.
    static constexpr float kPlaceFar = 64.f;

    bool Drive(const BodyFrame& acFrame, uint32_t aCopyFormId = 0, bool aHeld = false) noexcept override;
    [[nodiscard]] uint32_t ReferenceFormId(const BodyKey& acKey) const noexcept override;
    void SetCopies(const std::vector<uint32_t>& acCopyFormIds) noexcept override;
    bool KnockDown(uint32_t aCopyFormId) noexcept override;
    void SetMoving(const BodyKey& acKey, bool aMoving) noexcept override;
    void Forget(const BodyKey& acKey) noexcept override;
    void ForgetAll() noexcept override;
    bool Rest(const BodyPose& acPose, uint32_t aCopyFormId = 0) noexcept override;
    bool Free(const BodyFrame& acFrame) noexcept override;
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
    struct Class
    {
        BodyClass Is{};
        uint16_t Group{}; // its own, before ours
    };
    std::map<BodyKey, Class> m_classes;
    std::function<uint32_t(const GameId& acId)> m_formIdOf;
    std::map<BodyKey, uint8_t> m_motions; // the reference bodies held here: their own motion type
    // The owner's rest of each reference body, and whether it was put there (PlaceRest's aPlaced).
    struct Rested
    {
        BodyPose Pose;
        bool Placed{};
        std::chrono::steady_clock::time_point Since; // first asked for: it glides there at most kRestGlideSeconds
        bool Done{}; // put there: until then its waking is the rest's, not this game moving it (SetMoving)
        std::optional<std::chrono::steady_clock::time_point> Sleeping; // first put to sleep among others (RestSlept)
    };
    std::map<BodyKey, Rested> m_rested;
    std::map<BodyKey, std::pair<uint32_t, bhkWorldObject*>> m_ragdolls; // copy and body generation initialized here
    std::map<uint32_t, bool> m_knocked;   // living copies knocked down with their owners: lying yet
    std::chrono::steady_clock::time_point m_lastFrame{}; // the last frame that drove a body
    float m_frameSeconds = BodyPrediction::kSteerSeconds;  // the time between those frames (SteerSeconds)
};
