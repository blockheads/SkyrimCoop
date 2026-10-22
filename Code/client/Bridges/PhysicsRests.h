#pragma once

#include <deque>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/vec3.hpp>

#include <cstdint>

struct TESObjectREFR;

// Where the host's physics objects come to rest: each watched from its physics moving it (the change funnel's havok
// move) until it stays put (docs/design/world-state-sync.md, "Physics objects' rest").
class PhysicsRests
{
public:
    PhysicsRests() noexcept;
    ~PhysicsRests() noexcept;

    PhysicsRests(const PhysicsRests&) = delete;
    PhysicsRests& operator=(const PhysicsRests&) = delete;

    // Each update: the references at rest now away from where their physics took them (a settle in place is none).
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

    struct Watch
    {
        Pose Start;       // where its physics took it
        Pose Last;        // where it was Quiet updates ago
        uint32_t Quiet{};
    };

    void Moved(const TESObjectREFR& acObject) noexcept;

    std::unordered_map<uint32_t, Watch> m_moving;
    std::unordered_map<uint32_t, Pose> m_rested; // where each last rested
};
