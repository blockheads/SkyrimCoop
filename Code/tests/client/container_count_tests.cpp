// A reference's count of an item (engine/ContainerCount.h): CommonLibSSE-NG's rule, not 16214's.
#include <catch2/catch.hpp>

#include <ContainerCount.h>

TEST_CASE("A chest's count of an item: its base container's and its changes entry's", "[ContainerCount]")
{
    CHECK(ContainerCount(2, std::nullopt, false) == 2); // nothing touched it
    CHECK(ContainerCount(2, -1, false) == 1);           // one of the base's taken
    CHECK(ContainerCount(0, 3, false) == 3);            // put in
}

TEST_CASE("A roll's item taken from a chest counts none, not one", "[ContainerCount]")
{
    // guest-loots-hosts-roll, measured 2026-11-20: after the take the entry is at 0 with the roll's list kept (16059
    // frees it only on an actor), which 16214 read as one: the host's chest still "held" what the guest took.
    CHECK(ContainerCount(0, 1, true) == 1); // the fresh roll
    CHECK(ContainerCount(0, 0, true) == 0); // taken
}
