// Dialogue across games (DialogueService with HostConversation and GuestDialogue; the host's rules, HostDialogue): a
// guest talking to its copy of a host NPC starts the host's game's conversation with the NPC, its menu shows the
// host's game's list, its pick is said by the host's game, and the conversation ends for everyone.
#include "FakeDialogue.h"
#include <Services/DialogueService.h>
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
