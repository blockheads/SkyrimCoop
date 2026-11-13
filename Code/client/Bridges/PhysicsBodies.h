#pragma once

#include <Bridges/IBodyReads.h>
#include <Physics/BodyPrediction.h>
#include <MainLoopTasks.h>
#include <Structs/BodyFrame.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// The bodies physics moves (physics-sync.md): read while awake and moved from where they woke; a reference rests once
// each body is asleep or still (BodyPrediction::Still), an actor once asleep; an actor's are its ragdoll's while simulated. Reads through IBodyReads (the fake engine's in GameTests).
class PhysicsBodies
{
public:
    // A body's node further off than this was left behind by the engine (followed, it sits on the body to 0.1 units).
    static constexpr float kLeftBehind = 1.f;
    static constexpr float kLeftBehindAngle = 0.01f; // radians
    // A rest this near the last one sent is the same rest (a body woken where it lay); any more is sent, so a slow
    // settle's last tilt reaches the guests. The same rest goes again after kRestAgainMs: a guest's own copy of the
    // waker (Orgnar) wobbles its body too.
    static constexpr float kSameRest = 0.05f;
    static constexpr float kSameRestAngle = 0.002f; // radians
    static constexpr uint64_t kRestAgainMs = 2000;

    // aAllActors: every actor's ragdoll (the host's); a guest's watches its references and its player's.
    explicit PhysicsBodies(IBodyReads& aReads, bool aAllActors = true) noexcept;

    PhysicsBodies(const PhysicsBodies&) = delete;
    PhysicsBodies& operator=(const PhysicsBodies&) = delete;

    // A reference's dynamic bodies, each with its index (BodyKey::Index) and no object yet.
    struct Bodies
    {
        uint32_t FormId{};
        bool Actor{}; // its ragdoll's
        bool Held{};  // this game's player holds it (PlayerHolds)
        std::vector<BodyFrame> Read;
    };
    struct Update
    {
        std::vector<Bodies> Awake;  // only the awake bodies
        std::vector<Bodies> Rested; // every body, where it sleeps
    };

    // Each update at aTick (ms): the moving references' awake bodies, read now, and those that came to rest.
    [[nodiscard]] Update Take(uint64_t aTick) noexcept;
    // The references rested away from where their physics took them since the last call (a settle in place, as every
    // body at a cell load, is none): their move rows.
    [[nodiscard]] std::vector<uint32_t> TakeRested() noexcept;
    // An id the game uses again.
    void Forget(uint32_t aFormId) noexcept;

private:
    struct Pose
    {
        glm::vec3 Position{};
        glm::vec3 Rotation{};

        static Pose Of(const IBodyReads::Reference& acReference) noexcept;
        [[nodiscard]] bool IsNear(const Pose& acOther) const noexcept;
    };

    struct Moving
    {
        Pose At;          // where its physics took it
        bool Ragdoll{};   // an actor whose ragdoll physics simulated
        bool Resting{};   // a living actor lying still: its rest is sent
        std::vector<BodyFrame> Last; // an actor's bodies as last read awake
        std::vector<std::optional<BodyFrame>> Woke; // a reference's bodies where they woke (physics.md, "Woken still")
        std::vector<BodyPrediction::Still> Still;   // a reference's bodies' own stillness
        bool Moved{};
    };

    void QueueNode(uint32_t aFormId, uint8_t aIndex) noexcept;
    void QueueRest(uint32_t aFormId, const Pose& acStart) noexcept;

    IBodyReads& m_reads;
    bool m_allActors;
    bool m_playerRagdoll{};
    std::unordered_map<uint32_t, Moving> m_awake; // each moving reference
    struct Sent
    {
        std::vector<BodyFrame> Rest;
        uint64_t Tick{};
    };
    std::unordered_map<uint32_t, Sent> m_lastRests; // a reference's last rest sent
    std::vector<uint32_t> m_restedAway;
    std::unordered_map<uint32_t, uint64_t> m_sceneGeneration;
    MainLoopTasks::Scope m_mainTasks;
};
