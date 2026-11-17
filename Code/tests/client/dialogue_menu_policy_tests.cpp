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

TEST_CASE("W, S and the wheel move the party's highlight; a hover only its own menu's", "[dialogue]")
{
    using DialogueMenuPolicy::HighlightMove;
    using DialogueMenuPolicy::OnHighlightMoved;
    CHECK(OnHighlightMoved(0) == HighlightMove::Local);           // the cursor moved this frame
    CHECK(OnHighlightMoved(DialogueMenuPolicy::kHoverFrames) == HighlightMove::Local); // the rollover a frame behind it
    CHECK(OnHighlightMoved(DialogueMenuPolicy::kHoverFrames + 1) == HighlightMove::Send);
    CHECK(OnHighlightMoved(UINT32_MAX) == HighlightMove::Send);  // no cursor moved since the menu opened
}

TEST_CASE("A list's first highlight is told unless it is the top or the player's hover", "[dialogue]")
{
    using DialogueMenuPolicy::HighlightMove;
    using DialogueMenuPolicy::OnFirstHighlight;
    CHECK(OnFirstHighlight(0, UINT32_MAX, false) == HighlightMove::Local); // the fill's top
    CHECK(OnFirstHighlight(2, UINT32_MAX, false) == HighlightMove::Send);  // the fill's last said topic
    CHECK(OnFirstHighlight(2, 1, false) == HighlightMove::Local);          // hovered on the first sampled frame
    CHECK(OnFirstHighlight(2, UINT32_MAX, true) == HighlightMove::Local);  // a hover a same-step refresh kept
}

TEST_CASE("The rows' rollover is held from the recentring until the cursor moves", "[dialogue]")
{
    DialogueMenuPolicy::RolloverHold hold;
    const DialogueMenuPolicy::RolloverHold::Cursor cResting = std::pair{900.f, 480.f};
    CHECK_FALSE(hold.Release(cResting));
    hold.Recentred(cResting);
    CHECK_FALSE(hold.Release(cResting));
    CHECK(hold.Held);
    CHECK(hold.Release(std::pair{901.f, 480.f}));
    CHECK_FALSE(hold.Held);
}

TEST_CASE("Asking the first-option cursor leaves the picked option where it was", "[dialogue]")
{
    // FirstOption (35267) moves the picked option to the first; the build after takes choices from it.
    int* pPicked = nullptr;
    int first = 0;
    CHECK(DialogueMenuPolicy::AskKeepingPicked(pPicked, [&] { pPicked = &first; return true; }));
    CHECK(pPicked == nullptr);
}
