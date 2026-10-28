// Dialogue across games (DialogueService with HostConversation and GuestDialogue; the host's rules, HostDialogue): a
// guest talking to its copy of a host NPC starts the host's game's conversation with the NPC, its menu shows the
// host's game's list, its pick is said by the host's game, and the conversation ends for everyone.
#include "FakeDialogue.h"
#include <Services/DialogueService.h>
#include <Events/MenuEvent.h>
#include "Party.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
const IDialogueBridge::Option kNews{.Topic = 0x1001, .Info = 0x2001, .Text = "What's the news?"};
const IDialogueBridge::Option kGoodbye{.Topic = 0x1002, .Info = 0x2002, .Text = "Goodbye."};
const IDialogueBridge::Option kMore{.Topic = 0x1003, .Info = 0x2003, .Text = "Tell me more."};

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

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
    Party party(1, Plugins);
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

SCENARIO("Barter keeps a guest in the conversation with dialogue suspended", "[dialogue][barter]")
{
    Party party(1, Plugins);
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
    Party party(1, Plugins);
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
    Party party(2, Plugins);
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

SCENARIO("The host's begin fragment knows its picker before the picked notification returns", "[dialogue][rewards]")
{
    Party party(1, Plugins);
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
    // As Skyrim: the fragment is queued synchronously, before the host processes NotifyDialoguePicked.
    CHECK(talk.LastLineFor == FakeEngine::kPlayer);
    party.As(party.Host(), [&] { host.TeachesWord(0x13E22); });
    party.Until([&] { return guest.WordsKnown.contains(0x13E22); }, "the guest processed everything before the word marker");
    CHECK(host.RewardExperience[17] == 10.f);
    CHECK(guest.RewardExperience[17] == 0.f);
}
