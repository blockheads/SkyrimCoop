// Dialogue across games (DialogueService with HostConversation and GuestDialogue; the host's rules, HostDialogue): a
// guest talking to its copy of a host NPC starts the host's game's conversation with the NPC, its menu shows the
// host's game's list, its pick is said by the host's game, and the conversation ends for everyone.
#include "FakeDialogue.h"
#include <Services/DialogueService.h>
#include <Events/MenuEvent.h>
#include "Party.h"
#include "Fixtures.h"
#include <Events/SessionNoticeEvent.h>
#include <SessionIds.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
const IDialogueBridge::Option kNews{.Topic = 0x1001, .Info = 0x2001, .Text = "What's the news?"};
const IDialogueBridge::Option kGoodbye{.Topic = 0x1002, .Info = 0x2002, .Text = "Goodbye."};
const IDialogueBridge::Option kMore{.Topic = 0x1003, .Info = 0x2003, .Text = "Tell me more."};

// The chat's system lines this game's player reads (SessionNoticeEvent).
struct Notices
{
    explicit Notices(World& aGame) { aGame.GetDispatcher().sink<SessionNoticeEvent>().connect<&Notices::On>(*this); }
    void On(const SessionNoticeEvent& acNotice) { Lines.push_back(acNotice.Text); }
    std::vector<std::string> Lines;
};

FakeDialogue& Dialogue(World& aGame)
{
    return aGame.ctx().at<FakeDialogue&>();
}

// The game's data, the same in every game: the guard's topics, and what the news leads to.
void Topics(World& aGame)
{
    auto& dialogue = Dialogue(aGame);
    dialogue.Greeting = {kNews, kGoodbye};
    dialogue.After = {{kNews.Topic, {kMore, kGoodbye}}, {kMore.Topic, {kGoodbye}}, {kGoodbye.Topic, {}}};
}
} // namespace

SCENARIO("A guest talks to a host NPC: the host's game runs the conversation, the guest picks its lines", "[dialogue]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    Topics(party.Host());
    Topics(party.Guest());
    FakeDialogue& hostTalk = Dialogue(party.Host());
    FakeDialogue& guestTalk = Dialogue(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest's game has a copy of the guard");
    party.Agree("the games agree on the guard");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];

    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(cCopy); });
    party.Until([&] { return guestTalk.MenuOpen && guestTalk.Speaker == cCopy && hostTalk.Speaker == guard; },
                "the guest's menu opens on its copy, the host's game talks to the guard");
    THEN("the host's player plays on: its game's conversation runs without its menu") CHECK(!hostTalk.MenuOpen);
    party.Until([&] { return guestTalk.Options == std::vector{kNews, kGoodbye}; },
                "the guest's menu shows the guard's topics");

    party.As(party.Guest(), [&] { guestTalk.PlayerPicks(0); });
    party.Until([&] { return guestTalk.Options == std::vector{kMore, kGoodbye}; },
                "the host's game says the guest's pick, and the guest's menu shows what it leads to");
    CHECK(hostTalk.Said == std::vector{kNews});
    CHECK(guestTalk.Said.empty());

    party.As(party.Guest(), [&] { guestTalk.PlayerPicks(1); });
    party.Until([&] { return !hostTalk.Speaker && !guestTalk.MenuOpen; }, "the goodbye ends it on both games");
}

SCENARIO("Every menu shows the party's list; a player says only its own options", "[dialogue]")
{
    // Lucan's claw topic: a line for a player holding the claw first, his line for one without it after (TESTopic's
    // first line whose conditions pass); the deal is the host's player's alone, the gift the holder's alone
    // (docs/design/shared-dialogue.md, "The rule").
    const IDialogueBridge::Option cHasClaw{.Topic = 0x1004, .Info = 0x2005, .Text = "I have your claw."};
    const IDialogueBridge::Option cNoClaw{.Topic = 0x1004, .Info = 0x2004, .Text = "About your claw..."};
    const IDialogueBridge::Option cDeal{.Topic = 0x1005, .Info = 0x2006, .Text = "Let's make a deal."};
    const IDialogueBridge::Option cGift{.Topic = 0x1006, .Info = 0x2007, .Text = "A gift for you."};
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    FakeDialogue& hostTalk = Dialogue(party.Host());
    FakeDialogue& guestTalk = Dialogue(party.Guest());
    uint32_t holder = 0; // the character holding the claw, as the host's game knows it
    for (FakeDialogue* pTalk : {&hostTalk, &guestTalk})
    {
        pTalk->Greeting = {cHasClaw, cNoClaw, cDeal, cGift, kGoodbye};
        pTalk->After = {{cHasClaw.Topic, {kGoodbye}}, {cDeal.Topic, {kGoodbye}}, {cGift.Topic, {kGoodbye}}, {kGoodbye.Topic, {}}};
        pTalk->Needs[{cHasClaw.Topic, cHasClaw.Info}] = [&holder](uint32_t aPlayer) { return aPlayer == holder; };
        pTalk->Needs[{cGift.Topic, cGift.Info}] = [&holder](uint32_t aPlayer) { return aPlayer == holder; };
        pTalk->Needs[{cDeal.Topic, cDeal.Info}] = [](uint32_t aPlayer) { return aPlayer == 0x14; };
    }
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest's game has a copy of Lucan");
    party.Agree("the games agree on Lucan");
    uint32_t session = 0;
    party.As(party.Guest(), [&] { session = *Utils::GetSessionIdByFormId(FakeEngine::kPlayer); });
    party.As(party.Host(), [&] { holder = Utils::GetFormIdBySessionId(session); });
    const auto cTexts = [](const std::vector<IDialogueBridge::Option>& acOptions) {
        std::vector<std::string> texts;
        for (const auto& cOption : acOptions)
            texts.push_back(cOption.Text);
        return texts;
    };

    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(guest.CopiesOf(kGuard)[0]); });
    party.Until([&] { return guestTalk.MenuOpen && guestTalk.Options == std::vector{cHasClaw, kGoodbye, cGift}; },
                "alone in it, the guest's menu has its own lines (the host's order, then its own)");

    party.As(party.Host(), [&] { hostTalk.PlayerTalksTo(guard); });
    party.Until([&] { return hostTalk.MenuOpen && hostTalk.Options.size() == 5 && guestTalk.Options == hostTalk.Options; },
                "the host joins: both menus show the party's list, the same list");
    THEN("both lines of the claw topic, each named for its player; the other's options named (2026-10-30, the user)")
    {
        const auto cTexts0 = cTexts(hostTalk.Options);
        CHECK(cTexts0[0].starts_with("About your claw... ["));
        CHECK(cTexts0[1].starts_with("I have your claw. ["));
        CHECK(cTexts0[0] != cTexts0[1]);
        CHECK(cTexts0[2].starts_with("Let's make a deal. ["));
        CHECK(cTexts0[3] == "Goodbye.");
        CHECK(cTexts0[4].starts_with("A gift for you. ["));
    }

    WHEN("the host presses the guest's gift")
    {
        Notices hostNotices(party.Host()), guestNotices(party.Guest());
        party.As(party.Host(), [&] { hostTalk.PlayerPicks(4); });
        for (int i = 0; i < 20; ++i)
            party.Frame();
        THEN("nothing is said; only the host's chat tells its player why")
        {
            CHECK(hostTalk.Said.empty());
            REQUIRE(hostNotices.Lines.size() == 1);
            CHECK(hostNotices.Lines[0].starts_with("I'm sorry "));
            CHECK(hostNotices.Lines[0].ends_with(", you don't know how to say that."));
            CHECK(guestNotices.Lines.empty());
        }
    }
    WHEN("the guest presses its gift")
    {
        party.As(party.Guest(), [&] { guestTalk.PlayerPicks(4); });
        party.Until([&] { return !hostTalk.Said.empty(); }, "the host's game says the gift");
        THEN("it is the guest's line, its script run for the guest's character")
        {
            REQUIRE(hostTalk.Said.size() == 1);
            CHECK((hostTalk.Said[0].Topic == cGift.Topic && hostTalk.Said[0].Info == cGift.Info));
            CHECK(hostTalk.LastLineFor == holder);
        }
    }
    WHEN("the guest presses the host's deal")
    {
        Notices guestNotices(party.Guest());
        party.As(party.Guest(), [&] { guestTalk.PlayerPicks(2); });
        for (int i = 0; i < 20; ++i)
            party.Frame();
        THEN("nothing is said; the guest's chat tells it why")
        {
            CHECK(hostTalk.Said.empty());
            CHECK(guestNotices.Lines.size() == 1);
        }
    }
    WHEN("the host presses the guest's line of the claw topic")
    {
        Notices hostNotices(party.Host());
        party.As(party.Host(), [&] { hostTalk.PlayerPicks(1); });
        for (int i = 0; i < 20; ++i)
            party.Frame();
        THEN("nothing is said: that line is the holder's")
        {
            CHECK(hostTalk.Said.empty());
            CHECK(hostNotices.Lines.size() == 1);
        }
    }
    WHEN("each presses its own line of the claw topic")
    {
        const bool cGuestPicks = GENERATE(true, false);
        party.As(cGuestPicks ? party.Guest() : party.Host(), [&] { (cGuestPicks ? guestTalk : hostTalk).PlayerPicks(cGuestPicks ? 1 : 0); });
        party.Until([&] { return !hostTalk.Said.empty(); }, "the host's game says the pick");
        THEN("its own line of the topic")
        {
            const auto& cOwn = cGuestPicks ? cHasClaw : cNoClaw;
            REQUIRE(hostTalk.Said.size() == 1);
            CHECK((hostTalk.Said[0].Topic == cOwn.Topic && hostTalk.Said[0].Info == cOwn.Info));
        }
    }
}

SCENARIO("A pick takes every menu's list away until the next one, which every menu shows alike", "[dialogue]")
{
    // dialogue-several-questions-deep (2026-10-30): after the host's pick the guest's menu kept the old list, then its
    // engine showed its own once its copy's line ended, for the 27 s Andurs answered; the host's menu showed nothing.
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    Topics(party.Host());
    Topics(party.Guest());
    FakeDialogue& hostTalk = Dialogue(party.Host());
    FakeDialogue& guestTalk = Dialogue(party.Guest());
    hostTalk.LinesTakeTime = true;
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest's game has a copy of the guard");
    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(guest.CopiesOf(kGuard)[0]); });
    party.Until([&] { return guestTalk.MenuOpen && guestTalk.ListShown; }, "the guest's menu opens on the party's list");
    party.As(party.Host(), [&] { hostTalk.PlayerTalksTo(guard); });
    party.Until([&] { return party.Host().ctx().at<DialogueService>().Joined() && hostTalk.ListShown && guestTalk.ListShown &&
                             guestTalk.Options == std::vector{kNews, kGoodbye} &&
                             hostTalk.Options == guestTalk.Options; },
                "both in it, both menus show the party's list", [&] {
                    return std::format("host shown {} {} options, guest shown {} {} options, host menu {}", hostTalk.ListShown,
                                       hostTalk.Options.size(), guestTalk.ListShown, guestTalk.Options.size(), hostTalk.MenuOpen);
                });

    party.As(party.Host(), [&] { hostTalk.PlayerPicks(0); });
    party.Until([&] { return !guestTalk.ListShown; }, "the host's pick takes the guest's list away too", [&] {
        return std::format("host said {}, host shown {}, guest menu {}", hostTalk.Said.size(), hostTalk.ListShown, guestTalk.MenuOpen);
    });
    party.As(party.Guest(), [&] { guestTalk.LineEnds(); }); // its copy's line ends before the host's
    for (int i = 0; i < 20; ++i)
        party.Frame();
    THEN("the guest's engine shows no list of its own while the NPC answers; both menus show the line picked")
    {
        CHECK(!guestTalk.ListShown);
        CHECK(!hostTalk.ListShown);
        CHECK(hostTalk.PickedText == kNews.Text);
        CHECK(guestTalk.PickedText == hostTalk.PickedText);
    }
    party.As(party.Host(), [&] { hostTalk.LineEnds(); });
    party.Until([&] { return hostTalk.ListShown && guestTalk.ListShown && guestTalk.Options == std::vector{kMore, kGoodbye} &&
                             hostTalk.Options == guestTalk.Options; },
                "the NPC has answered: both menus show the next list");
}

SCENARIO("Barter keeps a guest in the conversation with dialogue suspended", "[dialogue][barter]")
{
    Party party(1, SmokeWorld);
    Topics(party.Host());
    Topics(party.Guest());
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& talk = Dialogue(party.Guest());
    auto& service = party.Guest().ctx().at<DialogueService>();
    uint32_t merchant = 0;
    party.As(party.Host(), [&] { merchant = host.LoadNpc(kGuard, kSmoke); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the merchant arrives");
    party.As(party.Guest(), [&] { talk.PlayerTalksTo(guest.CopiesOf(kGuard)[0]); });
    party.Until([&] { return talk.MenuOpen && talk.Options.size() == 2 && service.Joined(); }, "dialogue is joined");
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(MenuEvent{"BarterMenu", true}); });
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK_FALSE(talk.MenuOpen);
    CHECK(service.Joined());
    CHECK(talk.Speaker != 0);
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(MenuEvent{"BarterMenu", false}); });
    party.Until([&] { return talk.MenuOpen && service.Joined(); }, "closing barter returns to dialogue");
}

SCENARIO("A guest defers the next dialogue list until its personal menu closes", "[dialogue][barter]")
{
    Party party(1, SmokeWorld);
    Topics(party.Host());
    Topics(party.Guest());
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& talk = Dialogue(party.Guest());
    uint32_t merchant = 0;
    party.As(party.Host(), [&] { merchant = host.LoadNpc(kGuard, kSmoke); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the merchant's copy arrives");
    party.As(party.Guest(), [&] { talk.PlayerTalksTo(guest.CopiesOf(kGuard)[0]); });
    party.Until([&] { return talk.MenuOpen && talk.Options.size() == 2; }, "dialogue opens");
    talk.PlayerMenuOpen = true;
    party.As(party.Guest(), [&] { talk.PlayerPicks(0); });
    party.Until([&] { return Dialogue(party.Host()).Said.size() == 1; }, "the host says the picked line");
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK(talk.Options == std::vector{kNews, kGoodbye});
    talk.PlayerMenuOpen = false;
    party.Until([&] { return talk.Options == std::vector{kMore, kGoodbye}; }, "closing barter restores the pending dialogue list");
}

SCENARIO("A picked barter line opens only the trader's game and its Speech stays personal", "[dialogue][barter]")
{
    Party party(2, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& other = Party::Engine(party.Guest(1));
    Topics(party.Host());
    Topics(party.Guest());
    auto& hostTalk = Dialogue(party.Host());
    uint32_t merchant = 0;
    party.As(party.Host(), [&] { merchant = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the merchant");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    const bool cGuest = GENERATE(false, true);
    World& picker = cGuest ? party.Guest() : party.Host();
    FakeEngine& trader = Party::Engine(picker);
    FakeDialogue& talk = Dialogue(picker);
    const uint32_t cMerchant = cGuest ? cCopy : merchant;
    party.As(picker, [&] { talk.PlayerTalksTo(cMerchant); });
    party.Until([&] { return talk.MenuOpen && talk.Options.size() == 2; }, "the trader's dialogue opens");
    hostTalk.OnSaid = [&](const auto&) {
        host.RunScript({.Class = "Actor", .Function = "ShowBarterMenu", .Self = LocalForm{merchant}}, hostTalk.LastLineFor);
    };
    party.As(picker, [&] { talk.PlayerPicks(0); });
    party.Until([&] { return trader.BarterMerchant == cMerchant; }, "only the picker opens its local barter menu");
    party.As(picker, [&] {
        trader.RunScript({.Class = "Game", .Function = "AdvanceSkill", .Arguments = {std::string("Speechcraft"), 12.f}},
                         FakeEngine::kPlayer);
    });
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK(trader.RewardExperience[17] == 12.f);
    CHECK((cGuest ? host : guest).BarterMerchant == 0);
    CHECK((cGuest ? host : guest).RewardExperience[17] == 0.f);
    CHECK(other.BarterMerchant == 0);
    CHECK(other.RewardExperience[17] == 0.f);
}

SCENARIO("The host's own pick runs its fragment for its player", "[dialogue][rewards]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    Topics(party.Host());
    Topics(party.Guest());
    auto& talk = Dialogue(party.Host());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the guard");
    party.As(party.Host(), [&] { talk.PlayerTalksTo(guard); });
    party.Until([&] { return party.Host().ctx().at<DialogueService>().Conversation() != 0; }, "the conversation is shared");
    party.Frame();
    talk.OnSaid = [&](const auto&) {
        host.RunScript({.Class = "Game", .Function = "AdvanceSkill", .Arguments = {std::string("Speechcraft"), 10.f}}, talk.LastLineFor);
    };
    party.As(party.Host(), [&] { talk.PlayerPicks(0); });
    // Its menu does not say it: the host's rule names the speaker, then its game says the line.
    party.Until([&] { return !talk.Said.empty(); }, "the host's game says its own pick");
    CHECK(talk.LastLineFor == FakeEngine::kPlayer);
    party.As(party.Host(), [&] { host.TeachesWord(0x13E22); });
    party.Until([&] { return guest.WordsKnown.contains(0x13E22); }, "the guest processed everything before the word marker");
    CHECK(host.RewardExperience[17] == 10.f);
    CHECK(guest.RewardExperience[17] == 0.f);
}

SCENARIO("One highlight for the conversation: a player moving it moves every menu's", "[dialogue]")
{
    Party party(2, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    FakeEngine& other = Party::Engine(party.Guest(1));
    for (World* pGame : {&party.Host(), &party.Guest(), &party.Guest(1)})
    {
        Topics(*pGame);
        Dialogue(*pGame).Greeting = {kNews, kMore, kGoodbye};
    }
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && other.CopiesOf(kGuard).size() == 1; }, "every game has the guard");
    FakeDialogue& hostTalk = Dialogue(party.Host());
    FakeDialogue& guestTalk = Dialogue(party.Guest());
    FakeDialogue& otherTalk = Dialogue(party.Guest(1));
    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(guest.CopiesOf(kGuard)[0]); });
    party.Until([&] { return guestTalk.MenuOpen && guestTalk.Options.size() == 3; }, "the guest talks to the guard");
    party.As(party.Guest(1), [&] { otherTalk.PlayerTalksTo(other.CopiesOf(kGuard)[0]); });
    party.As(party.Host(), [&] { hostTalk.PlayerTalksTo(guard); });
    party.Until([&] { return otherTalk.MenuOpen && hostTalk.MenuOpen && otherTalk.Options.size() == 3 && hostTalk.Options.size() == 3; },
                "everyone joins");
    for (int i = 0; i < 5; ++i)
        party.Frame();

    party.As(party.Guest(), [&] { guestTalk.PlayerHighlights(2); });
    party.Until([&] { return hostTalk.Highlight == 2u && otherTalk.Highlight == 2u; }, "every menu highlights Goodbye");
    party.As(party.Host(), [&] { hostTalk.PlayerHighlights(1); });
    party.Until([&] { return guestTalk.Highlight == 1u && otherTalk.Highlight == 1u; }, "the host moves everyone's to the next");
    THEN("whoever presses E picks the highlighted option")
    {
        party.As(party.Guest(1), [&] { otherTalk.PlayerPicks(*otherTalk.Highlight); });
        party.Until([&] { return !hostTalk.Said.empty(); }, "the host's game says it");
        CHECK(hostTalk.Said[0].Topic == kMore.Topic);
    }
}
