#pragma once

#include <Bridges/IBodyReads.h>
#include <MainLoopTasks.h>
#include <Structs/BodyFrame.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// The host's physics objects (docs/design/physics-sync.md): each reference physics moves (the change funnel's havok
// move) is read each update while any of its bodies is awake (its island active); once all sleep it rests. An actor's
// bodies are its ragdoll's, read while they are simulated (dead, knocked down, getting up), not while it stands.
// It reads through IBodyReads (the engine's BodyReads; the fake engine's in GameTests).
class PhysicsBodies
{
public:
    // A body's node further off than this was left behind by the engine (followed, it sits on the body to 0.1 units).
    static constexpr float kLeftBehind = 1.f;
    static constexpr float kLeftBehindAngle = 0.01f; // radians

    // aAllActors: every actor's ragdoll (the host's); a guest's watches its references and its player's.
    explicit PhysicsBodies(IBodyReads& aReads, bool aAllActors = true) noexcept;

    PhysicsBodies(const PhysicsBodies&) = delete;
    PhysicsBodies& operator=(const PhysicsBodies&) = delete;

    // A reference's dynamic bodies, each with its index (BodyKey::Index) and no object yet.
    struct Bodies
    {
        uint32_t FormId{};
        bool Actor{}; // its ragdoll's
        std::vector<BodyFrame> Read;
    };
    struct Update
    {
        std::vector<Bodies> Awake;  // only the awake bodies
        std::vector<Bodies> Rested; // every body, where it sleeps
    };

    // Each update: the moving references' awake bodies, read now, and those whose bodies all fell asleep.
    [[nodiscard]] Update Take() noexcept;
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
    };

    void QueueNode(uint32_t aFormId, uint8_t aIndex) noexcept;
    void QueueRest(uint32_t aFormId, const Pose& acStart) noexcept;

    IBodyReads& m_reads;
    bool m_allActors;
    bool m_playerRagdoll{};
    std::unordered_map<uint32_t, Moving> m_awake; // each moving reference
    std::vector<uint32_t> m_restedAway;
    std::unordered_map<uint32_t, uint64_t> m_sceneGeneration;
    MainLoopTasks::Scope m_mainTasks;
};
