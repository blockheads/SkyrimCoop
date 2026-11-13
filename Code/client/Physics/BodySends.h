#pragma once

#include <Physics/BodyPrediction.h>

#include <map>
#include <vector>

// When a body is wrong enough to send, and how much a guest gets.
struct BodySendLimits
{
    float Distance = 2.f;          // units
    float Angle = 0.05f;           // radians
    uint64_t RefreshMs = 500;      // a lost frame's repair
    float BytesPerSecond = 8192.f; // the guest's budget
    float BurstBytes = 2048.f;     // what a quiet guest saves up
};

// The host's send rule for one guest (docs/design/physics-sync.md, "Which bodies"): a body's frame goes when the
// guest's guess from the last one it got is wrong, or that is old; the most wrong first, within the guest's budget.
// No engine: ClientTests.
class BodySends
{
public:
    using Limits = BodySendLimits;

    explicit BodySends(const Limits& acLimits = {}) noexcept;

    // This update's awake bodies, read at aTick: the frames due to the guest, each as the guest gets it.
    [[nodiscard]] std::vector<BodyFrame> Due(const std::vector<BodyFrame>& acAwake, uint64_t aTick) noexcept;
    // A body that rested (its rest goes reliably) or left: no guess of it is kept.
    void Forget(const BodyKey& acKey) noexcept;

private:
    struct Sent
    {
        BodyFrame Frame; // as the guest got it
        uint64_t Tick{};
    };

    Limits m_limits;
    std::map<BodyKey, Sent> m_sent;
    float m_allowance;
    uint64_t m_lastTick{};
};

// Marks each awake body falling freely or not, from its velocity's change since the last read (shared by every
// guest's sends).
class BodyMotions
{
public:
    void Mark(std::vector<BodyFrame>& aAwake, uint64_t aTick) noexcept;
    void Forget(const BodyKey& acKey) noexcept;

private:
    struct Last
    {
        glm::vec3 Velocity{};
        uint64_t Tick{};
        bool Gravity{};
    };
    std::map<BodyKey, Last> m_last;
};
