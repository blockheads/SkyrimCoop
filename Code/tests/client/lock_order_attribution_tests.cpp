#include <BSCore/LockOrderAttribution.h>
#include <catch2/catch.hpp>

#include <array>

TEST_CASE("Pass-through travel frames do not hide a real engine callback", "[locks]")
{
    using namespace LockOrderAttribution;
    constexpr Range module{0x1000, 0x3000};
    constexpr std::array passThrough{Range{0x2000, 0x2358}};
    // Checker -> engine locks -> travel wrapper -> engine main loop.
    constexpr std::array<uintptr_t, 4> native{0x1100, 0x5000, 0x2200, 0x6000};
    CHECK(IntroducedByUs(native, module, {}));
    CHECK_FALSE(IntroducedByUs(native, module, passThrough));
    // A service called by the engine still introduces its own order, inside or below that wrapper.
    constexpr std::array<uintptr_t, 5> callback{0x1100, 0x5000, 0x1800, 0x2200, 0x6000};
    constexpr std::array<uintptr_t, 5> outer{0x1100, 0x5000, 0x2200, 0x1800, 0x6000};
    CHECK(IntroducedByUs(callback, module, passThrough));
    CHECK(IntroducedByUs(outer, module, passThrough));
    constexpr std::array<uintptr_t, 3> boundary{0x1100, 0x5000, 0x2358};
    CHECK(IntroducedByUs(boundary, module, passThrough));
}

TEST_CASE("An inversion between the main loop's tasks and our update is kept apart by the frame", "[locks]")
{
    using namespace LockOrderAttribution;
    // ClearFills in MainLoopTasks against a respawn's cast in World::Update: never at once.
    CHECK(KeptApartByFrame(kMainLoopTasks, kOurUpdate));
    CHECK(KeptApartByFrame(kOurUpdate, kMainLoopTasks));
    // The same stretch on two threads, or anywhere else, can overlap.
    CHECK_FALSE(KeptApartByFrame(kMainLoopTasks, kMainLoopTasks));
    CHECK_FALSE(KeptApartByFrame(kOurUpdate, kOurUpdate));
    CHECK_FALSE(KeptApartByFrame(kElsewhere, kOurUpdate));
    CHECK_FALSE(KeptApartByFrame(kMainLoopTasks, kElsewhere));
    // A reverse order also seen elsewhere is a risk however often it was seen in the other stretch.
    CHECK_FALSE(KeptApartByFrame(kMainLoopTasks, kOurUpdate | kElsewhere));
    CHECK_FALSE(KeptApartByFrame(kOurUpdate, kMainLoopTasks | kOurUpdate));
    CHECK_FALSE(KeptApartByFrame(kMainLoopTasks, 0));
}

TEST_CASE("An edge's stretch history turns a kept-apart pair back into a risk", "[locks]")
{
    using namespace LockOrderAttribution;
    StretchOverlap frame;
    std::atomic<uint8_t> reverse{0}, reported{0};
    // The respawn takes the reverse order in our update; the main loop's task then inverts it.
    NoteStretch(reverse, kOurUpdate);
    CHECK(frame.KeptApart(kMainLoopTasks, reverse));
    CHECK(FirstReportOfKind(reported, true));
    CHECK_FALSE(FirstReportOfKind(reported, true));
    // The same order later taken elsewhere: the same pair is a risk, reported although it was reported kept apart.
    NoteStretch(reverse, kElsewhere);
    CHECK_FALSE(frame.KeptApart(kMainLoopTasks, reverse));
    CHECK(FirstReportOfKind(reported, false));
    CHECK_FALSE(FirstReportOfKind(reported, false));
}

TEST_CASE("A sighting made while an edge is published stays in its history", "[locks]")
{
    using namespace LockOrderAttribution;
    // Another thread finds the key before its publisher notes the publisher's own stretch.
    std::atomic<uint8_t> stretches{0};
    NoteStretch(stretches, kElsewhere);
    NoteStretch(stretches, kMainLoopTasks);
    CHECK(stretches == (kElsewhere | kMainLoopTasks));
    CHECK_FALSE(KeptApartByFrame(kOurUpdate, stretches));
}

TEST_CASE("Main loop tasks and our update seen at once keep nothing apart", "[locks]")
{
    using namespace LockOrderAttribution;
    StretchOverlap frame;
    CHECK_FALSE(frame.Enter(kMainLoopTasks));
    frame.Leave(kMainLoopTasks);
    CHECK_FALSE(frame.Enter(kOurUpdate));
    CHECK_FALSE(frame.Enter(kElsewhere));
    CHECK(frame.KeptApart(kOurUpdate, kMainLoopTasks));
    // Tasks start while our update runs: the frame's claim failed in this run.
    CHECK(frame.Enter(kMainLoopTasks));
    CHECK_FALSE(frame.Enter(kMainLoopTasks));
    frame.Leave(kMainLoopTasks);
    frame.Leave(kMainLoopTasks);
    frame.Leave(kOurUpdate);
    CHECK_FALSE(frame.KeptApart(kOurUpdate, kMainLoopTasks));
    CHECK_FALSE(frame.KeptApart(kMainLoopTasks, kOurUpdate));
}
