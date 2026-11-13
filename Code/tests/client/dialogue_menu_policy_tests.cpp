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

TEST_CASE("The speaker's body turns to the talker the host names as it would to this game's player", "[dialogue][headtrack]")
{
    using DialogueMenuPolicy::AskTurnForTalker;
    constexpr uint32_t cGuest = 0xFF000829;
    CHECK(AskTurnForTalker(false, true, true, cGuest, cGuest));
    CHECK_FALSE(AskTurnForTalker(true, true, true, cGuest, cGuest));      // turns already
    CHECK_FALSE(AskTurnForTalker(false, false, true, cGuest, cGuest));    // not the player-only rule
    CHECK_FALSE(AskTurnForTalker(false, true, false, cGuest, cGuest));    // not the conversation's speaker
    CHECK_FALSE(AskTurnForTalker(false, true, true, 0x14, 0));            // this game's player talks
    CHECK_FALSE(AskTurnForTalker(false, true, true, 0xFF000900, cGuest)); // looks at someone else
}
