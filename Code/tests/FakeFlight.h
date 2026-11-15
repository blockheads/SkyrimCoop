#pragma once

// A body's flight as Havok roughly flies it, for the physics tests (ClientTests, GameTests): free fall, a bounce
// off the floor at z = 0 losing half its speed and spin, then sliding with friction until it stops.
#include <Physics/BodyPrediction.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

struct FakeFlight
{
    BodyFrame Body{};
    float Floor = 0.f;
    bool Resting = false;

    static constexpr float kRestitution = 0.5f;
    static constexpr float kFriction = 400.f; // units/s² while it slides

    // One physics step of aSeconds.
    void Step(float aSeconds)
    {
        if (Resting)
            return;
        auto& v = Body.LinearVelocity;
        const bool cOnFloor = Body.Position.z <= Floor + 0.01f && std::abs(v.z) < 1.f;
        if (cOnFloor)
        {
            v.z = 0.f;
            const float cSpeed = glm::length(glm::vec2(v.x, v.y));
            const float cSlower = std::max(0.f, cSpeed - kFriction * aSeconds);
            if (cSlower <= 0.f)
            {
                v = {};
                Body.AngularVelocity = {};
                Resting = true;
                return;
            }
            v.x *= cSlower / cSpeed;
            v.y *= cSlower / cSpeed;
        }
        else
            v.z += BodyPrediction::kGravity * aSeconds;
        Body.Position += v * aSeconds;
        if (Body.Position.z < Floor)
        {
            Body.Position.z = Floor;
            v.z = -v.z * kRestitution;
            if (v.z < 30.f)
                v.z = 0.f;
            Body.AngularVelocity *= kRestitution;
        }
        const float cSpin = glm::length(Body.AngularVelocity);
        if (cSpin > 1e-6f)
            Body.Rotation = glm::normalize(glm::angleAxis(cSpin * aSeconds, Body.AngularVelocity / cSpin) * Body.Rotation);
    }
};
