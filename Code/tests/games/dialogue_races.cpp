#include "FakeDialogue.h"
#include "Party.h"
#include "Fixtures.h"
#include <Services/DialogueService.h>
#include <Messages/SharedDialogue.h>
#include <Messages/RelayedMessages.h>
#include <Messages/Relayed.h>
#include <SessionIds.h>

namespace
{
constexpr uint32_t kCell = 0x31D2E;
constexpr uint32_t kNpc = 0x1A67C;
const IDialogueBridge::Option kFirst{.Topic = 0x1001, .Info = 0x2001, .Text = "First"};
const IDialogueBridge::Option kSecond{.Topic = 0x1002, .Info = 0x2002, .Text = "Second"};
FakeDialogue& Talk(World& aGame) { return aGame.ctx().at<FakeDialogue&>(); }
}

TEST_CASE("The dialogue fake accepts picks and reads highlights only while the list is usable", "[dialogue]")
{
    FakeDialogue talk;
    talk.Greeting = {kFirst, kSecond};
    unsigned clicks = 0;
    talk.SetFilters({.Pick = [&](uint32_t) { ++clicks; return true; }});
    REQUIRE(talk.Start(kNpc, true));
    REQUIRE(talk.ShowList(talk.Greeting, false));
    talk.PlayerHighlights(1);
    CHECK(talk.GetHighlight() == 1u);
    REQUIRE(talk.ShowList(talk.Greeting, false));
    CHECK(talk.GetHighlight() == 1u);
    talk.PlayerPicks(1);
    REQUIRE(clicks == 1);
    CHECK_FALSE(talk.ListShown);
    CHECK_FALSE(talk.GetHighlight());
    talk.PlayerHighlights(0);
    talk.PlayerPicks(0);
    CHECK(clicks == 1);
    REQUIRE(talk.ShowList(talk.Greeting, false));
    CHECK(talk.ListShown);
    CHECK(talk.PickedText.empty());
    talk.AllowProgress = false;
    talk.PlayerPicks(0);
    CHECK(clicks == 1);
    talk.AllowProgress = true;
    talk.PlayerPicks(0);
    CHECK(clicks == 2);
}

SCENARIO("Competing and repeated dialogue picks say one line and restore every menu even with an unchanged next list", "[dialogue][dialogue-race]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& hostTalk = Talk(party.Host());
    auto& guestTalk = Talk(party.Guest());
    for (auto* talk : {&hostTalk, &guestTalk})
    {
        talk->Greeting = {kFirst, kSecond};
        talk->After = {{kFirst.Topic, talk->Greeting}, {kSecond.Topic, talk->Greeting}};
    }
    hostTalk.LinesTakeTime = true;
    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = host.LoadNpc(kNpc, kCell); });
    party.Until([&] { return guest.CopiesOf(kNpc).size() == 1; }, "the guest has the speaker");
    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(guest.CopiesOf(kNpc)[0]); });
    party.Until([&] { return guestTalk.MenuOpen && guestTalk.ListShown; }, "the guest's list opens");
    party.As(party.Host(), [&] { hostTalk.PlayerTalksTo(npc); });
    party.Until([&] { return hostTalk.ListShown && guestTalk.ListShown && party.Host().ctx().at<DialogueService>().Joined(); },
                "both players can choose");
    const bool cHostFirst = GENERATE(false, true);
    const auto hostPick = [&] { party.As(party.Host(), [&] { hostTalk.PlayerPicks(0); hostTalk.PlayerPicks(0); }); };
    const auto guestPick = [&] { party.As(party.Guest(), [&] { guestTalk.PlayerPicks(1); guestTalk.PlayerPicks(1); }); };
    if (cHostFirst) { hostPick(); guestPick(); } else { guestPick(); hostPick(); }
    party.Until([&] { return hostTalk.Said.size() == 1 && !hostTalk.ListShown && !guestTalk.ListShown; },
                "one accepted choice hides both lists");
    for (int i = 0; i < 20; ++i) party.Frame();
    REQUIRE(hostTalk.Said.size() == 1);
    CHECK(hostTalk.PickedText == guestTalk.PickedText);
    CHECK(guestTalk.Said.empty());
    party.As(party.Host(), [&] { hostTalk.LineEnds(); });
    party.Until([&] { return hostTalk.ListShown && guestTalk.ListShown && hostTalk.Options == hostTalk.Greeting &&
                            guestTalk.Options == hostTalk.Options; }, "the same options return as usable lists on both games");
    party.As(party.Guest(), [&] { guestTalk.PlayerPicks(0); });
    party.Until([&] { return hostTalk.Said.size() == 2; }, "the guest can make the next legitimate choice");
    CHECK(hostTalk.Said[1].Info == kFirst.Info);
}

SCENARIO("Delayed dialogue notifications cannot change a newly opened conversation", "[dialogue][dialogue-race]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& hostTalk = Talk(party.Host());
    auto& guestTalk = Talk(party.Guest());
    hostTalk.Greeting = guestTalk.Greeting = {kFirst, kSecond};
    uint32_t first = 0, second = 0, firstSession = 0, secondSession = 0;
    party.As(party.Host(), [&] {
        first = host.LoadNpc(kNpc, kCell);
        second = host.LoadNpc(kNpc, kCell);
    });
    party.Until([&] { return guest.CopiesOf(kNpc).size() == 2; }, "both speakers arrive");
    party.As(party.Host(), [&] {
        firstSession = *Utils::GetSessionIdByFormId(first);
        secondSession = *Utils::GetSessionIdByFormId(second);
    });
    uint32_t firstCopy = 0, secondCopy = 0;
    party.As(party.Guest(), [&] {
        firstCopy = Utils::GetFormIdBySessionId(firstSession);
        secondCopy = Utils::GetFormIdBySessionId(secondSession);
    });
    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(firstCopy); });
    auto& service = party.Guest().ctx().at<DialogueService>();
    party.Until([&] { return service.Conversation() && guestTalk.ListShown; }, "the first conversation opens");
    const auto old = service.Conversation();
    party.As(party.Guest(), [&] { guestTalk.PlayerLeaves(); });
    party.Until([&] { return !service.Conversation() && !hostTalk.Speaker; }, "the first conversation closes");
    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(secondCopy); });
    party.Until([&] { return service.Conversation() && service.Conversation() != old && guestTalk.ListShown; },
                "the second conversation is usable");
    const auto current = service.Conversation();
    NotifyDialogueClosed closed;
    closed.Conversation = old;
    NotifyDialogueList list;
    list.Conversation = old;
    list.Step = 1;
    NotifyDialoguePicked picked;
    picked.Conversation = old;
    picked.Step = 2;
    NotifyDialogueHighlight highlight;
    highlight.Conversation = old;
    highlight.Step = 1;
    highlight.Index = 1;
    party.As(party.Guest(), [&] {
        auto& events = party.Guest().GetDispatcher();
        events.trigger(list);
        events.trigger(picked);
        events.trigger(highlight);
        events.trigger(closed);
    });
    for (int i = 0; i < 20; ++i) party.Frame();
    CHECK(service.Conversation() == current);
    CHECK(service.Step() == 1);
    CHECK(guestTalk.Speaker == secondCopy);
    CHECK(guestTalk.MenuOpen);
    CHECK(guestTalk.ListShown);
    CHECK(guestTalk.Options == hostTalk.Greeting);
    CHECK(guestTalk.Highlight == 0u);
}
