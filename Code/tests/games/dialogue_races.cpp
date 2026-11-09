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

// placed-speaker-deleted-mid-conversation (Skyrim 2026-11-06): the fault in 37541 is held back by its hook; the
// conversation must still end, on the host where it runs and on the guest that asked.
SCENARIO("A quest deletes a placed NPC a guest talks to: the conversation ends on both games", "[dialogue][dialogue-race]")
{
    constexpr uint32_t kPlaced = 0x0001A695;
    Party party(1, [](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.PlaceNpc(kPlaced, 0x0001A694, kCell, {100.f, 10.f, 0.f});
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& hostTalk = Talk(party.Host());
    auto& guestTalk = Talk(party.Guest());
    hostTalk.Greeting = guestTalk.Greeting = {kFirst, kSecond};
    party.Until([&] { return guest.IsActorCopy(kPlaced); }, "the guest's reference is the host's copy");
    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(kPlaced); });
    party.Until([&] { return guestTalk.MenuOpen && hostTalk.Speaker == kPlaced; }, "the guest talks to it, the host's game in the background");

    party.As(party.Host(), [&] { host.MarkForDelete(kPlaced); });
    party.Until([&] { return !hostTalk.Speaker && !guestTalk.Speaker && !guestTalk.MenuOpen; }, "the conversation ends on both games",
                [&] { return std::format("speakers: host {:X}, guest {:X}; guest's menu {}", hostTalk.Speaker, guestTalk.Speaker, guestTalk.MenuOpen); });
}

namespace
{
const IDialogueBridge::Option kThird{.Topic = 0x1003, .Info = 0x2003, .Text = "Third"};
const IDialogueBridge::Option kFourth{.Topic = 0x1004, .Info = 0x2004, .Text = "Fourth"};

// Frames for a while of real time: whatever is on the (delayed) line arrives.
void Settle(Party& aParty, std::chrono::milliseconds aFor = std::chrono::milliseconds(300))
{
    const auto cUntil = std::chrono::steady_clock::now() + aFor;
    while (std::chrono::steady_clock::now() < cUntil)
        aParty.Frame();
}

// Every game's player talks to the NPC (the first guest first, the host's game in the background), each menu on the
// party's list of four. Returns the host's NPC.
uint32_t EveryoneTalks(Party& aParty, std::vector<World*> aOrder)
{
    std::vector<World*> games{&aParty.Host()};
    for (size_t i = 0; i + 1 < aParty.GameCount(); ++i)
        games.push_back(&aParty.Guest(i));
    for (World* pGame : games)
        Talk(*pGame).Greeting = {kFirst, kSecond, kThird, kFourth};
    uint32_t npc = 0;
    aParty.As(aParty.Host(), [&] { npc = Party::Engine(aParty.Host()).LoadNpc(kNpc, kCell); });
    aParty.Until([&] { return std::ranges::all_of(games, [&](World* p) { return p == games.front() || Party::Engine(*p).CopiesOf(kNpc).size() == 1; }); },
                 "every game has the speaker");
    for (World* pGame : aOrder)
    {
        aParty.As(*pGame, [&] { Talk(*pGame).PlayerTalksTo(pGame == &aParty.Host() ? npc : Party::Engine(*pGame).CopiesOf(kNpc)[0]); });
        aParty.Until([&] { return Talk(*pGame).ListShown && Talk(*pGame).Options.size() == 4 && pGame->ctx().at<DialogueService>().Joined(); },
                     "a player joins the conversation");
    }
    Settle(aParty);
    return npc;
}

std::string Highlights(const std::vector<FakeDialogue*>& acTalks)
{
    std::string text;
    for (const FakeDialogue* pTalk : acTalks)
        text += std::format("{} ", pTalk->Highlight ? std::to_string(*pTalk->Highlight) : "-");
    return text;
}
} // namespace

// Two players move the highlight in the same frame (dialogue-highlights-cross): every menu ends on one option. Over a
// line with some delay, as between two machines: each move is sent before the other arrives.
SCENARIO("Highlights moved on two games at once end on the same option on every menu", "[dialogue][dialogue-race]")
{
    const size_t cGuests = GENERATE(1u, 2u);
    Party party(cGuests, SmokeWorld, true, NetConditions{.DelayMs = 30});
    std::vector<World*> order{&party.Guest(), &party.Host()};
    if (cGuests == 2)
        order.push_back(&party.Guest(1));
    EveryoneTalks(party, order);
    std::vector<FakeDialogue*> talks;
    for (World* pGame : order)
        talks.push_back(&Talk(*pGame));

    // In one frame, before any game hears of another's move.
    for (size_t i = 0; i < order.size(); ++i)
        party.As(*order[i], [&] { talks[i]->PlayerHighlights(static_cast<uint32_t>(i + 1)); });
    Settle(party);
    INFO("highlights: " << Highlights(talks));
    CHECK(std::ranges::all_of(talks, [&](FakeDialogue* p) { return p->Highlight == talks.front()->Highlight; }));
}

// A player keeps moving the highlight: the host's echo of an older move never pulls its menu back.
SCENARIO("A player moving the highlight fast keeps its own place on its menu", "[dialogue][dialogue-race]")
{
    Party party(1, SmokeWorld, true, NetConditions{.DelayMs = 30});
    EveryoneTalks(party, {&party.Guest(), &party.Host()});
    auto& hostTalk = Talk(party.Host());
    auto& guestTalk = Talk(party.Guest());
    for (uint32_t index : {1u, 2u, 3u})
    {
        party.As(party.Guest(), [&] { guestTalk.PlayerHighlights(index); });
        party.Step(party.Guest());
        CHECK(guestTalk.Highlight == index);
    }
    const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < cUntil)
    {
        party.Frame();
        REQUIRE(guestTalk.Highlight == 3u);
    }
    party.Until([&] { return hostTalk.Highlight == 3u; }, "the host's menu follows to the last");
}

// A player joins a conversation whose highlight another moved (dialogue-joiner-gets-highlight): its menu shows it there.
SCENARIO("A player joining a conversation sees the party's highlight", "[dialogue][dialogue-race]")
{
    Party party(1, SmokeWorld);
    const uint32_t npc = EveryoneTalks(party, {&party.Guest()});
    auto& hostTalk = Talk(party.Host());
    auto& guestTalk = Talk(party.Guest());
    party.As(party.Guest(), [&] { guestTalk.PlayerHighlights(2); });
    Settle(party);
    party.As(party.Host(), [&] { hostTalk.PlayerTalksTo(npc); });
    party.Until([&] { return hostTalk.MenuOpen && hostTalk.ListShown && party.Host().ctx().at<DialogueService>().Joined(); },
                "the host joins by talking");
    party.Until([&] { return hostTalk.Highlight == 2u; }, "the host's menu shows the guest's highlight",
                [&] { return Highlights({&hostTalk, &guestTalk}); });
    Settle(party);
    CHECK(guestTalk.Highlight == 2u);
    CHECK(hostTalk.Highlight == 2u);
}

// The host talks to the NPC while it answers a guest's pick (dialogue-host-joins-mid-line): its menu shows the pick, as
// the guest's does, never the last step's list, and takes no pick until the next list. Twice: every conversation's.
SCENARIO("A player joining while the NPC answers sees the pick, not the last list", "[dialogue][dialogue-race]")
{
    Party party(1, SmokeWorld);
    auto& hostTalk = Talk(party.Host());
    auto& guestTalk = Talk(party.Guest());
    const std::vector<IDialogueBridge::Option> cNext{kFourth};
    for (FakeDialogue* pTalk : {&hostTalk, &guestTalk})
        pTalk->After = {{kThird.Topic, cNext}, {kFourth.Topic, {kFirst, kSecond}}};
    hostTalk.LinesTakeTime = true;
    const uint32_t npc = EveryoneTalks(party, {&party.Guest()});
    auto& guest = Party::Engine(party.Guest());
    for (int conversation = 0; conversation < 2; ++conversation)
    {
        INFO("conversation " << conversation + 1);
        if (conversation)
        {
            party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(guest.CopiesOf(kNpc)[0]); });
            party.Until([&] { return guestTalk.ListShown && guestTalk.Options.size() == 4; }, "the guest talks again");
            Settle(party);
        }
        const size_t cSaid = hostTalk.Said.size();
        party.As(party.Guest(), [&] { guestTalk.PlayerPicks(2); });
        party.Until([&] { return hostTalk.Said.size() == cSaid + 1; }, "the host's game says the guest's line");
        REQUIRE(guestTalk.PickedText == kThird.Text);

        party.As(party.Host(), [&] { hostTalk.PlayerTalksTo(npc); });
        party.Until([&] { return hostTalk.MenuOpen && party.Host().ctx().at<DialogueService>().Joined(); }, "the host joins mid-line");
        Settle(party, std::chrono::milliseconds(100));
        CHECK_FALSE(hostTalk.ListShown);
        CHECK(hostTalk.PickedText == kThird.Text);
        party.As(party.Host(), [&] { hostTalk.PlayerPicks(0); });
        CHECK(hostTalk.Said.size() == cSaid + 1);

        party.As(party.Host(), [&] { hostTalk.LineEnds(); });
        party.Until([&] { return hostTalk.ListShown && guestTalk.ListShown && hostTalk.Options == cNext && guestTalk.Options == cNext; },
                    "both menus show the next list");
        party.As(party.Host(), [&] { hostTalk.PlayerLeaves(); });
        party.As(party.Guest(), [&] { guestTalk.PlayerLeaves(); });
        party.Until([&] { return !hostTalk.Speaker && !guestTalk.MenuOpen; }, "the conversation ends");
    }
}
