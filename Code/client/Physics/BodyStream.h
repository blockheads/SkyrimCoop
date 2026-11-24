#pragma once

#include <Physics/BodyTolerances.h>
#include <Structs/BodyFrame.h>

#include <map>
#include <vector>

// The state stream to one receiver, Fiedler's priority accumulator (docs/design/physics-authority-plan.md):
//
//   changed   its claim, held, at rest or its rest's pose differ from what the receiver got: first, kChangedRepeats
//             updates running (a lost one is repaired at once)
//   moving    on the ground: every kMovingMs while it moves; in free fall: when its guess is off (kFallDistance);
//             awake and unmoved: every kRefreshMs
//   at rest   priority grows over kRestRefreshMs: every resting body comes round
//
// The highest first within the receiver's budget. No engine: ClientTests.
struct BodyStreamLimits
{
    float BytesPerSecond = 8192.f; // the receiver's budget
    float BurstBytes = 2048.f;     // what a quiet stream saves up
};

class BodyStream
{
public:
    using Limits = BodyStreamLimits;

    explicit BodyStream(const Limits& acLimits = {}) noexcept;

    // This update's states for the receiver, each as it hears it: the ones due, quantized as it gets them.
    [[nodiscard]] std::vector<BodyState> Due(const std::vector<BodyState>& acStates, uint64_t aTick) noexcept;
    void Forget(const BodyKey& acKey) noexcept { m_sent.erase(acKey); }
    void Clear() noexcept { m_sent.clear(); }

private:
    struct Sent
    {
        BodyState State{}; // as the receiver got it
        uint64_t Tick{};
        uint8_t Repeats{}; // changed: sends left
    };

    Limits m_limits;
    std::map<BodyKey, Sent> m_sent;
    float m_allowance;
    uint64_t m_lastTick{};
};
