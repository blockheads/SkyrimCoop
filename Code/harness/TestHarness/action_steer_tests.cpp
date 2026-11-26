// The checked actions' pure halves (docs/design/checked-actions.md): steering an angle by mouse counts.
#include <catch2/catch.hpp>

#include <TestHarness/ActionSteer.h>

#include <cmath>

using namespace ActionSteer;

TEST_CASE("An angle's error is the short way round", "[action-steer]")
{
    CHECK(AngleError(10.f, 350.f) == Approx(20.f));
    CHECK(AngleError(350.f, 10.f) == Approx(-20.f));
    CHECK(AngleError(-170.f, 170.f) == Approx(20.f));
    CHECK(AngleError(90.f, 90.f) == Approx(0.f));
}

namespace
{
// A player's view turned by counts at an unknown sensitivity (and maybe inverted), with a frame now and then
// dropping the input.
struct View
{
    float At = 0.f;
    float DegreesPerCount;
    int Frame = 0;
    void Turn(int32_t aCounts)
    {
        if (++Frame % 7 != 0)
            At = std::fmod(At + aCounts * DegreesPerCount + 360.f, 360.f);
    }
};
} // namespace

TEST_CASE("A turn reaches its goal without passing it, at any sensitivity, never faster than allowed", "[action-steer]")
{
    for (const float cSensitivity : {0.005f, 0.0225f, 0.15f, -0.0225f})
    {
        INFO("degrees per count " << cSensitivity);
        View view{.At = 10.f, .DegreesPerCount = cSensitivity};
        Axis axis;
        const float cGoal = 100.f, cMax = 1.f;
        float before = view.At;
        int frames = 0;
        for (; frames < 400 && std::fabs(AngleError(cGoal, view.At)) > 0.5f; ++frames)
        {
            view.Turn(axis.Step(cGoal, view.At, cMax, 0.25f));
            // No frame turns much more than the cap: it measures with single counts first.
            CHECK(std::fabs(AngleError(view.At, before)) <= cMax * 1.5f + 0.01f);
            CHECK(AngleError(cGoal, view.At) > -0.5f); // never past the goal
            before = view.At;
        }
        CHECK(std::fabs(AngleError(cGoal, view.At)) <= 0.5f);
        CHECK(frames < 200); // 90 degrees at 1 a frame, with every seventh frame lost
    }
}

TEST_CASE("A watch keeps a value's lowest and highest with their frames", "[action-steer]")
{
    Extremes seen;
    seen.Add(5.f, 1);
    seen.Add(2.f, 2);
    seen.Add(9.f, 3);
    seen.Add(4.f, 4);
    CHECK(*seen.Min == 2.f);
    CHECK(seen.MinFrame == 2);
    CHECK(*seen.Max == 9.f);
    CHECK(seen.MaxFrame == 3);
}
