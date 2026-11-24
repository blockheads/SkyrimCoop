#pragma once

#include <Physics/BodyTolerances.h>
#include <Structs/BodyFrame.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <map>
#include <vector>

// How a copy's ragdoll takes up its authority's frames: its joints tie its bodies, so they go together. Where each was
// when its first frame came is blended out over kSeconds (docs/re/systems/kill-moves.md, "A copy's ragdoll starts whole").
class RagdollStart
{
public:
    static constexpr float kSeconds = 0.2f;

    struct Body
    {
        BodyFrame Frame;
        glm::vec3 Position{}; // where this game has it
        glm::quat Rotation{1.f, 0.f, 0.f, 0.f};
        bool First{};
    };
    struct Plan
    {
        bool Place{};                   // one body past kPlaceFar: the whole ragdoll is put on its targets, else steered
        std::vector<BodyFrame> Targets; // per body: where, and how it moves there
    };

    [[nodiscard]] Plan Next(const std::vector<Body>& acBodies, uint64_t aNow)
    {
        Plan plan;
        for (const Body& cBody : acBodies)
        {
            if (cBody.First)
                m_gaps[cBody.Frame.Key.Index] = {cBody.Position - cBody.Frame.Position,
                                                 glm::normalize(cBody.Rotation * glm::inverse(cBody.Frame.Rotation)), aNow};
            plan.Targets.push_back(Target(cBody.Frame, aNow));
            plan.Place |= glm::distance(cBody.Position, cBody.Frame.Position) >= BodyTolerances::kPlaceFar;
        }
        if (plan.Place)
        {
            m_gaps.clear();
            plan.Targets.clear();
            for (const Body& cBody : acBodies)
                plan.Targets.push_back(cBody.Frame);
        }
        return plan;
    }

private:
    struct Gap
    {
        glm::vec3 Position{};
        glm::quat Rotation{1.f, 0.f, 0.f, 0.f};
        uint64_t Since{};
    };
    std::map<uint8_t, Gap> m_gaps;

    [[nodiscard]] BodyFrame Target(BodyFrame aFrame, uint64_t aNow) const
    {
        const auto it = m_gaps.find(aFrame.Key.Index);
        if (it == m_gaps.end())
            return aFrame;
        const float cLeft = 1.f - std::clamp(static_cast<float>(aNow - it->second.Since) / 1000.f / kSeconds, 0.f, 1.f);
        if (cLeft <= 0.f)
            return aFrame;
        aFrame.Position += it->second.Position * cLeft;
        aFrame.LinearVelocity -= it->second.Position / kSeconds;
        aFrame.Rotation = glm::normalize(glm::slerp(glm::quat(1.f, 0.f, 0.f, 0.f), it->second.Rotation, cLeft) * aFrame.Rotation);
        return aFrame;
    }
};
