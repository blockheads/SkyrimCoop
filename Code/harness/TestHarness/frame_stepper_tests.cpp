#include <catch2/catch.hpp>

#include <TestHarness/FrameStepper.h>

using TestHarness::FrameStepper;

TEST_CASE("A frozen world runs exactly the frames it is stepped", "[frame-stepper]")
{
    FrameStepper stepper;
    CHECK_FALSE(stepper.Poll(true)); // not frozen: runs
    stepper.Freeze(true);
    CHECK(stepper.Poll(false)); // frozen frames count nothing
    CHECK(stepper.WorldFrames() == 1);
    stepper.Step(2);
    CHECK_FALSE(stepper.Poll(false)); // the step asked in a frozen frame: the next one runs
    CHECK_FALSE(stepper.Poll(true));  // first stepped frame ran
    CHECK(stepper.Poll(true));        // second ran: frozen again
    CHECK(stepper.WorldFrames() == 3);
    CHECK(stepper.StepsLeft() == 0);
}

TEST_CASE("A pausing menu does not use up a step", "[frame-stepper]")
{
    FrameStepper stepper;
    stepper.Freeze(true);
    stepper.Step(1);
    CHECK_FALSE(stepper.Poll(false)); // a menu paused the world this frame: still one to run
    CHECK(stepper.StepsLeft() == 1);
    CHECK(stepper.Poll(true));
    stepper.Freeze(false);
    CHECK_FALSE(stepper.Poll(true));
}
