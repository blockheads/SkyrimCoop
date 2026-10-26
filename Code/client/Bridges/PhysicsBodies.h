#pragma once

#include <Structs/BodyFrame.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct TESObjectREFR;

// The host's physics objects (docs/design/physics-sync.md): each reference physics moves (the change funnel's havok
// move) is read each update while any of its bodies is awake (its island active); once all sleep it rests. An actor's
// bodies are its ragdoll's, read while they are simulated (dead, knocked down), not while its animation drives them.
// One at a time (the host's, or a guest's own moves: GuestEngine::TakeOwnMoves): the funnel's moved set is shared.
class PhysicsBodies
{
public:
    // aActors: actors' ragdolls too (the host's); a guest's watches its references only.
    explicit PhysicsBodies(bool aActors = true) noexcept;
    ~PhysicsBodies() noexcept;

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

        static Pose Of(const TESObjectREFR& acObject) noexcept;
        [[nodiscard]] bool IsNear(const Pose& acOther) const noexcept;
    };

    bool m_actors;
    std::unordered_map<uint32_t, Pose> m_awake; // each moving reference: where its physics took it
    std::vector<uint32_t> m_restedAway;
};
