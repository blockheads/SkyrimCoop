#include <catch2/catch.hpp>
#include <Systems/CopyVariables.h>

namespace
{
AnimationVariables Integers(std::vector<uint32_t> aValues)
{
    AnimationVariables variables;
    variables.Integers = std::move(aValues);
    return variables;
}
} // namespace

TEST_CASE("A copy takes the next snapshot's integers within a send interval", "[copy-variables]")
{
    // The owner wrote iSyncTurnState 1 at 1020; the snapshot carrying it is stamped 1100. The copy's clip ends at 1030.
    auto shown = Integers({0, 2});
    CHECK(CopyVariables::TakeIntegersAhead(shown, Integers({1, 1}), 1100, 1000, 1030, 100));
    CHECK(shown.Integers == std::vector<uint32_t>{1, 1});
}

TEST_CASE("A copy does not take integers more than an interval ahead, or over a later action's", "[copy-variables]")
{
    auto shown = Integers({0, 2});
    CHECK_FALSE(CopyVariables::TakeIntegersAhead(shown, Integers({1, 1}), 1200, 1000, 1030, 100)); // too far ahead
    CHECK_FALSE(CopyVariables::TakeIntegersAhead(shown, Integers({1, 1}), 1100, 1100, 1030, 100)); // an action at 1100 loaded its own
    CHECK_FALSE(CopyVariables::TakeIntegersAhead(shown, Integers({1, 1}), 1030, 1000, 1030, 100)); // not ahead: the current one's
    CHECK_FALSE(CopyVariables::TakeIntegersAhead(shown, Integers({1}), 1100, 1000, 1030, 100));    // another layout
    CHECK(shown.Integers == std::vector<uint32_t>{0, 2});
}

TEST_CASE("Booleans and floats stay the copy's own choice", "[copy-variables]")
{
    auto shown = Integers({0});
    shown.Booleans = {false};
    shown.Floats = {1.f};
    auto next = Integers({1});
    next.Booleans = {true};
    next.Floats = {2.f};
    CHECK(CopyVariables::TakeIntegersAhead(shown, next, 1100, 1000, 1030, 100));
    CHECK(shown.Booleans == std::vector<bool>{false});
    CHECK(shown.Floats == std::vector<float>{1.f});
}
