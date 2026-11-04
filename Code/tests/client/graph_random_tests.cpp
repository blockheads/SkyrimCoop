#include <catch2/catch.hpp>
#include <Games/Animation/GraphRandom.h>

using namespace GraphRandom;

TEST_CASE("A pick with no action behind it sends a sync point", "[animation][graph-random]")
{
    Owner owner;
    CHECK_FALSE(Picked(owner, 5, 1000)); // the first seed read is no pick
    CHECK_FALSE(Picked(owner, 5, 2000));
    CHECK(Picked(owner, 6, 3000));
}

TEST_CASE("A pick an action made sends none: the action carried its seed", "[animation][graph-random]")
{
    Owner owner;
    Picked(owner, 5, 1000);
    owner.ActionAt = 2000;
    CHECK_FALSE(Picked(owner, 6, 2000 + cActionPickMs - 1));
    CHECK(Picked(owner, 7, 2000 + cActionPickMs));
}

TEST_CASE("A graph drawing every frame sends a sync point now and then", "[animation][graph-random]")
{
    Owner owner;
    Picked(owner, 5, 1000);
    CHECK(Picked(owner, 6, 2000));
    CHECK_FALSE(Picked(owner, 7, 2000 + cPointGapMs - 1));
    CHECK(Picked(owner, 8, 2000 + cPointGapMs));
}

TEST_CASE("A copy that made the pick around its moment ignores the sync point", "[animation][graph-random]")
{
    Copy copy;
    Track(copy, 9, 5200);
    CHECK(Judge(copy, 9, 5000, 5200) == Point::kInStep);
    Track(copy, 10, 5000 - cInStepMs);
    CHECK(Judge(copy, 10, 5000, 5000) == Point::kInStep);
}

TEST_CASE("A copy on another sequence loads at the point's own moment", "[animation][graph-random]")
{
    Copy copy;
    Track(copy, 3, 1000);
    CHECK(Judge(copy, 9, 5000, 4999) == Point::kWait);
    CHECK(Judge(copy, 9, 5000, 5000) == Point::kLoad);
}

TEST_CASE("A copy a draw behind gets a moment to pick", "[animation][graph-random]")
{
    constexpr uint32_t cSeed = 5, cNext = cSeed * 1664525 + 1013904223;
    Copy copy;
    Track(copy, cSeed, 1000);
    CHECK(Judge(copy, cNext, 5000, 5000 + cInStepMs - 1) == Point::kWait);
    Track(copy, cNext, 5000 + cInStepMs - 1);
    CHECK(Judge(copy, cNext, 5000, 5000 + cInStepMs - 1) == Point::kInStep);
}

TEST_CASE("A copy that waited and loaded late does not wait again", "[animation][graph-random]")
{
    constexpr uint32_t cSeed = 5, cNext = cSeed * 1664525 + 1013904223, cAfter = cNext * 1664525 + 1013904223;
    Copy copy;
    Track(copy, cSeed, 1000);
    CHECK(Judge(copy, cNext, 5000, 5000 + cInStepMs) == Point::kLoad);
    Track(copy, cNext, 5000 + cInStepMs);
    CHECK(Judge(copy, cAfter, 9000, 9000) == Point::kLoad);
    Track(copy, cAfter, 9000);
    CHECK(Judge(copy, cAfter * 1664525 + 1013904223, 13000, 13000) == Point::kWait);
}

TEST_CASE("A copy a loop ahead on the same seed is not in step", "[animation][graph-random]")
{
    Copy copy;
    Track(copy, 9, 1000); // Skyrim: its draw came a whole loop before its owner's
    CHECK(Judge(copy, 9, 9000, 9000) == Point::kLoad);
}

TEST_CASE("Graph counters survive the wire", "[animation][graph-random]")
{
    const auto cCounters = Unpack(Pack({{"BSEventEveryNEventsModifier00", 2, 3}, {"Every:Other", 0, 1}}));
    REQUIRE(cCounters.size() == 2);
    CHECK(cCounters[0].Name == "BSEventEveryNEventsModifier00");
    CHECK(cCounters[0].Seen == 2);
    CHECK(cCounters[0].Needed == 3);
    CHECK(cCounters[1].Name == "Every:Other");
    CHECK(cCounters[1].Needed == 1);
    CHECK(Unpack("").empty());
}
