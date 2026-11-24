#include <catch2/catch.hpp>
#include <Services/ActivatePrompts.h>
#include <Services/DialogueMenuPolicy.h>
#include <Services/Guest/VoiceFiles.h>

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
    CHECK(DisplayPicked(0) == PickedDisplay::FromGreeting); // a menu joining mid-answer: Tab must close it
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

TEST_CASE("Another player's character never talks to this game's player; everyone else as the engine says", "[dialogue]")
{
    using DialogueMenuPolicy::TalksToPlayer;
    CHECK(TalksToPlayer(true, false));
    CHECK_FALSE(TalksToPlayer(false, false)); // vanilla's no (race flag, AllowPCDialogue) stays
    CHECK_FALSE(TalksToPlayer(true, true));
    CHECK_FALSE(TalksToPlayer(false, true));
}

TEST_CASE("No crosshair prompt over another player's character", "[dialogue]")
{
    CHECK_FALSE(ActivatePrompts::Offered(true));
    CHECK(ActivatePrompts::Offered(false));
}

TEST_CASE("A voice file is a line of the topic info its name carries", "[dialogue][voice]")
{
    using VoiceFiles::IsLineOf;
    constexpr const char* cpGreeting = "Data\\Sound\\Voice\\Skyrim.esm\\MaleSlyCynical\\DialogueWh_WhiterunAndursF_000D79A5_1.wav";
    CHECK(IsLineOf(cpGreeting, 0x000D79A5, "Skyrim.esm"));
    CHECK(IsLineOf(cpGreeting, 0x020D79A5, "skyrim.esm")); // a full plugin's index is 00 in the name; folders by any case
    CHECK_FALSE(IsLineOf(cpGreeting, 0x000D3D5E, "Skyrim.esm")); // an answer
    CHECK_FALSE(IsLineOf(cpGreeting, 0x050D79A5, "Dawnguard.esm")); // the same local id in another plugin
    CHECK_FALSE(IsLineOf(cpGreeting, 0, "Skyrim.esm"));
    // A light plugin's form (loaded at FE): only its 12 bits name the file (25502).
    constexpr const char* cpLight = "Data\\Sound\\Voice\\Mine.esl\\FemaleYoungEager\\MyQuest_Hello_00000ABC_1.fuz";
    CHECK(IsLineOf(cpLight, 0xFE123ABC, "Mine.esl"));
    CHECK_FALSE(IsLineOf("Data\\Sound\\Voice\\Mine.esl\\FemaleYoungEager\\MyQuest_Hello_00123ABC_1.fuz", 0xFE123ABC, "Mine.esl"));
}
