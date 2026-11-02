#include <catch2/catch.hpp>
#include <Services/DialogueMenuPolicy.h>

TEST_CASE("A dialogue refresh restores a hidden list even when its options did not change", "[dialogue]")
{
    using DialogueMenuPolicy::NeedsRefill;
    CHECK_FALSE(NeedsRefill(false, true));
    CHECK(NeedsRefill(true, true));
    CHECK(NeedsRefill(true, false));
    CHECK(NeedsRefill(false, false));
    CHECK(NeedsRefill(false, true, true));
}

TEST_CASE("Barter suspends dialogue but returning restores each role's close behavior", "[barter-reopen]")
{
    using DialogueMenuPolicy::CloseEndsConversation;
    CHECK_FALSE(CloseEndsConversation(false, true, true));
    CHECK_FALSE(CloseEndsConversation(true, true, true));
    CHECK(CloseEndsConversation(false, false, true));
    CHECK_FALSE(CloseEndsConversation(true, false, true));
    CHECK(CloseEndsConversation(false, false, false));
    CHECK(CloseEndsConversation(true, false, false));
}

TEST_CASE("The accepted dialogue pick replaces a competing local pick without replaying its animation", "[dialogue]")
{
    using DialogueMenuPolicy::DisplayPicked;
    using DialogueMenuPolicy::PickedDisplay;
    CHECK(DisplayPicked(0) == PickedDisplay::Ignore);
    CHECK(DisplayPicked(1) == PickedDisplay::Animate);
    CHECK(DisplayPicked(2) == PickedDisplay::ReplaceText);
    CHECK(DisplayPicked(3) == PickedDisplay::Ignore);
}
