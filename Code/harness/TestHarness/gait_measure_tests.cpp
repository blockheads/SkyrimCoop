#include <catch2/catch.hpp>

#include <TestHarness/GaitMeasure.h>

using namespace TestHarness;

namespace
{
// An actor going 2.5 units a frame along x; drawn, its feet swing 2 units a frame, culled they hold their last pose.
std::deque<GaitSample> Walk(int aFrames, bool (*apDrawn)(int))
{
    std::deque<GaitSample> samples;
    const auto cStart = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
    float swing = 0.f;
    for (int i = 0; i < aFrames; ++i)
    {
        const bool cDrawn = apDrawn(i);
        if (cDrawn)
            swing = (i % 20 < 10 ? 1.f : -1.f) * 2.f + swing;
        samples.push_back({cStart + std::chrono::milliseconds(16 * i), {2.5f * i, 0.f, 0.f}, {{swing, 0.f, 0.f}, {-swing, 0.f, 0.f}}, cDrawn});
    }
    return samples;
}
} // namespace

TEST_CASE("A culled actor's steps are left out of its gait", "[gait]")
{
    // Measured 2026-11-05 (owners-walk-slide): Dorthe in cull state 5 ran 1234 units with her foot bones still.
    const auto cCulled = Walk(120, [](int aFrame) { return aFrame < 60; });
    const GaitReading cReading = MeasureGait(cCulled, {});
    CHECK(cReading.Frames == 59);
    CHECK(cReading.HiddenFrames == 60);
    CHECK(cReading.Travelled == Approx(59 * 2.5f));
    CHECK(cReading.Feet == Approx(59 * 2.f));
}

TEST_CASE("A drawn actor's gait counts every step, and a teleport none", "[gait]")
{
    auto drawn = Walk(60, [](int) { return true; });
    drawn[30].Position[0] += 500.f; // a jump there and back: two steps over 200 units
    const GaitReading cReading = MeasureGait(drawn, {});
    CHECK(cReading.Frames == 57);
    CHECK(cReading.HiddenFrames == 0);
    CHECK(cReading.Travelled == Approx(57 * 2.5f));
}
