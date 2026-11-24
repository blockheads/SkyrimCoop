// The dialogue walker (docs/design/dialogue-stress.md): random players' and world's actions on one shared conversation,
// with the rules that must always hold checked after the games settle. A failure prints its seed and its actions.
#include "FakeDialogue.h"
#include "Party.h"
#include "Fixtures.h"
#include <Services/DialogueService.h>
#include <Events/DialogueEvent.h>
#include <Services/Guest/VoiceFiles.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t kCell = 0x31D2E;
constexpr uint32_t kNpc = 0x1A67C;
constexpr uint32_t kGreetingInfo = 0x4000; // the NPC's greeting line
constexpr uint32_t kQuest = 0x36191; // its stage moves on each time the host's engine says More (the info's fragment)
const IDialogueBridge::Option kAsk{.Topic = 0x3001, .Info = 0x4001, .Text = "Ask"};
const IDialogueBridge::Option kMore{.Topic = 0x3002, .Info = 0x4002, .Text = "More"};
const IDialogueBridge::Option kBack{.Topic = 0x3003, .Info = 0x4003, .Text = "Back"};
const IDialogueBridge::Option kBye{.Topic = 0x3004, .Info = 0x4004, .Text = "Bye"};

FakeDialogue& Talk(World& aGame) { return aGame.ctx().at<FakeDialogue&>(); }

// A conversation with depth and a goodbye: Ask leads on, More deeper, Back returns, Bye ends it.
void Script(FakeDialogue& aTalk)
{
    aTalk.Greeting = {kAsk, kMore, kBye};
    aTalk.After = {{kAsk.Topic, {kMore, kBack, kBye}}, {kMore.Topic, {kAsk, kBack, kBye}}, {kBack.Topic, {kAsk, kMore, kBye}}};
}

// Long enough for what is on the line to arrive: under COOP_NET a lost packet's resend too.
void Settle(Party& aParty)
{
    const auto cFor = std::chrono::milliseconds(aParty.Conditioned() ? 2000 : 150);
    const auto cUntil = std::chrono::steady_clock::now() + cFor;
    while (std::chrono::steady_clock::now() < cUntil)
        aParty.Frame();
}

struct Walk
{
    Party& Games;
    std::mt19937 Rng;
    std::vector<std::string> Log;
    uint32_t Npc = 0;
    std::vector<bool> Out; // a guest that dropped, by guest index

    uint32_t Roll(uint32_t aCount) { return std::uniform_int_distribution<uint32_t>(0, aCount - 1)(Rng); }

    World& Game(size_t aIndex) { return aIndex == 0 ? Games.Host() : Games.Guest(aIndex - 1); }
    std::string Name(size_t aIndex) { return aIndex == 0 ? "host" : std::format("guest {}", aIndex); }
    bool Online(size_t aIndex) { return aIndex == 0 || !Out[aIndex - 1]; }

    // The NPC as that game has it: the host's, or the guest's copy (none: not loaded there).
    uint32_t NpcIn(size_t aIndex)
    {
        if (aIndex == 0)
            return Npc;
        // The newest living copy: the host's current NPC (an old one may linger dead, or held).
        const FakeEngine& engine = Party::Engine(Game(aIndex));
        uint32_t newest = 0;
        for (const uint32_t cCopy : engine.CopiesOf(kNpc))
            if (const auto it = engine.Actors.find(cCopy); it != engine.Actors.end() && !it->second.Dead)
                newest = std::max(newest, cCopy);
        return newest;
    }

    void Step()
    {
        const size_t cWho = Roll(static_cast<uint32_t>(Games.GameCount()));
        World& game = Game(cWho);
        FakeDialogue& talk = Talk(game);
        const uint32_t cKind = Roll(100);
        std::string what;
        if (!Online(cWho))
        {
            if (cKind < 30)
            {
                what = "rejoins";
                Games.Join(game);
                Out[cWho - 1] = false;
            }
        }
        else if (cKind < 25)
        {
            if (const uint32_t cTarget = NpcIn(cWho))
            {
                what = "talks";
                Games.As(game, [&] { talk.PlayerTalksTo(cTarget); });
            }
        }
        else if (cKind < 37)
        {
            what = "leaves the menu";
            Games.As(game, [&] { talk.PlayerLeaves(); });
        }
        else if (cKind < 62)
        {
            const uint32_t cIndex = Roll(3);
            what = std::format("picks {}", cIndex);
            Games.As(game, [&] { talk.PlayerPicks(cIndex); });
        }
        else if (cKind < 70)
        {
            what = "skips the line";
            Games.As(game, [&] { talk.PlayerSkips(); });
        }
        else if (cKind < 78)
        {
            const uint32_t cIndex = Roll(3);
            what = std::format("highlights {}", cIndex);
            Games.As(game, [&] { talk.PlayerHighlights(cIndex); });
        }
        else if (cKind < 92)
        {
            // The host's NPC finishes its line: the engine's next list.
            FakeDialogue& hostTalk = Talk(Games.Host());
            if (hostTalk.LinePending())
            {
                what = "the NPC's line ends";
                Games.As(Games.Host(), [&] { hostTalk.LineEnds(); });
            }
        }
        else if (cKind < 95)
        {
            // The world ends the NPC's part: deleted by a quest, killed, or a fight; another stands in its place.
            FakeEngine& host = Party::Engine(Games.Host());
            const uint32_t cHow = Roll(3);
            what = cHow == 0 ? "the host's world deletes the NPC" : cHow == 1 ? "the NPC is killed" : "the NPC starts a fight";
            Games.As(Games.Host(), [&] {
                if (cHow == 0)
                    host.MarkForDelete(Npc);
                else if (cHow == 1)
                    host.KillActor(Npc);
                else
                    host.AttackedBy(Npc, 0x14);
            });
            for (uint32_t frames = Roll(3); frames > 0; --frames)
                Games.Frame();
            Broken(); // settles: the next NPC is another conversation's
            Games.As(Games.Host(), [&] { Npc = host.LoadNpc(kNpc, kCell); });
            what += ", then another stands in its place";
        }
        else if (cWho != 0)
        {
            what = "drops out";
            Games.Leave(game);
            Out[cWho - 1] = true;
        }
        if (!what.empty())
            Log.push_back(std::format("{} {}", Name(cWho), what));
        for (uint32_t frames = Roll(4); frames > 0; --frames)
            Games.Frame();
    }

    // The rules after the games settle: empty when they hold.
    std::string Broken()
    {
        Settle(Games);
        const auto& cHostDialogue = Games.Host().ctx().at<DialogueService>();
        std::string broken;
        for (size_t i = 0; i < Games.GameCount(); ++i)
        {
            World& game = Game(i);
            FakeDialogue& talk = Talk(game);
            const auto& cDialogue = game.ctx().at<DialogueService>();
            if (talk.SpeakerFaults)
                broken += std::format("{}: its engine updated a speaker without a process ({}); ", Name(i), talk.SpeakerFaults);
            if (!Online(i))
            {
                if (talk.MenuOpen)
                    broken += std::format("{}: out of the session, its menu still open; ", Name(i));
                continue;
            }
            if (!talk.MenuOpen)
                continue;
            const auto& cActors = Party::Engine(game).Actors;
            if (const auto it = cActors.find(talk.Speaker); it == cActors.end())
                broken += std::format("{}: its menu open on {:X}, which is gone; ", Name(i), talk.Speaker);
            else if (it->second.Dead || it->second.OwnCombatTarget)
                broken += std::format("{}: its menu open on {:X}, which is {}; ", Name(i), talk.Speaker, it->second.Dead ? "dead" : "fighting");
            if (i != 0 && !cDialogue.Joined())
                broken += std::format("{}: its menu open but not in the conversation; ", Name(i));
            if (i != 0 && cDialogue.Joined() && cDialogue.Conversation() != cHostDialogue.Conversation())
                broken += std::format("{}: in conversation {}, the host's is {}; ", Name(i), cDialogue.Conversation(), cHostDialogue.Conversation());
        }
        // The quest: More's fragment ran once per More said, and every game in the session holds the host's state.
        const FakeEngine& host = Party::Engine(Games.Host());
        const auto& cQuest = host.Quests.at(kQuest);
        const auto cMores = std::ranges::count(Talk(Games.Host()).Said, kMore.Topic, &IDialogueBridge::Option::Topic);
        if (cQuest.CurrentStage != 10 * cMores)
            broken += std::format("the quest at stage {} after {} More(s) said; ", cQuest.CurrentStage, cMores);
        for (size_t i = 1; i < Games.GameCount(); ++i)
            if (Online(i))
                if (const auto& cGuestQuests = Party::Engine(Game(i)).Quests; !cGuestQuests.contains(kQuest) || !(cGuestQuests.at(kQuest) == cQuest))
                    broken += std::format("{}: the quest is not the host's (stage {} there, {} here); ", Name(i),
                                          cGuestQuests.contains(kQuest) ? cGuestQuests.at(kQuest).CurrentStage : -1, cQuest.CurrentStage);
        // Each line the host's engine voiced reaches a copy once; a greeting at most twice (a guest's own ask replays it
        // once: the opening menu silences the copy, GuestDialogue::ReplayGreeting).
        for (size_t i = 1; i < Games.GameCount(); ++i)
        {
            std::map<std::string, int> heard;
            for (const auto& [cCopy, cFile] : Party::Engine(Game(i)).Lines)
                ++heard[cFile];
            for (const auto& [cFile, cTimes] : heard)
                if (cTimes > (VoiceFiles::IsLineOf(cFile, kGreetingInfo) ? 2 : 1))
                {
                    std::string said;
                    for (const auto& [cCopy, cLine] : Party::Engine(Game(i)).Lines)
                        said += std::format(" {:X}:{}", cCopy, cLine);
                    broken += std::format("{}: its copy said {} {} times (said:{}); ", Name(i), cFile, cTimes, said);
                }
        }
        // Every menu showing a list at the host's step shows the same one.
        const FakeDialogue* pShown = nullptr;
        size_t shownBy = 0;
        for (size_t i = 0; i < Games.GameCount(); ++i)
        {
            FakeDialogue& talk = Talk(Game(i));
            const auto& cDialogue = Game(i).ctx().at<DialogueService>();
            if (!Online(i) || !talk.MenuOpen || !talk.ListShown || !cDialogue.Joined() || cDialogue.Step() != cHostDialogue.Step())
                continue;
            if (!pShown)
            {
                pShown = &talk;
                shownBy = i;
                continue;
            }
            const auto cTopics = [](const FakeDialogue& acTalk) {
                std::vector<uint32_t> topics;
                for (const auto& cOption : acTalk.Options)
                    topics.push_back(cOption.Topic);
                return topics;
            };
            if (cTopics(*pShown) != cTopics(talk))
                broken += std::format("{} and {} show different lists at step {}; ", Name(shownBy), Name(i), cDialogue.Step());
        }
        return broken;
    }

    std::string Actions() const
    {
        std::string text;
        for (const auto& cLine : Log)
            text += "\n  " + cLine;
        return text;
    }
};

// One walk: seed, steps, guests. Fails with the seed and what was done.
void RunWalk(uint32_t aSeed, int aSteps, size_t aGuests)
{
    Party party(aGuests, [](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        QuestState quest;
        quest.Id = GameId{0, kQuest};
        quest.Flags = 1;
        aEngine.Quests[kQuest] = quest;
    });
    Walk walk{party, std::mt19937(aSeed)};
    walk.Out.assign(aGuests, false);
    for (size_t i = 0; i < party.GameCount(); ++i)
        Script(Talk(walk.Game(i)));
    Talk(party.Host()).LinesTakeTime = true;
    // The host's engine voices each line (Actor::SpeakSound, our hook's DialogueEvent), each a file of its own; More's
    // fragment runs as it says the line.
    // Named as the Creation Kit names them (VoiceFiles), each voicing its own file.
    uint32_t voiced = 0;
    const auto cVoice = [&](uint32_t aInfo) {
        World& host = party.Host();
        host.GetDispatcher().trigger(DialogueEvent(Talk(host).Speaker, std::format("Walk_Topic_00{:06X}_{}.fuz", aInfo, ++voiced)));
    };
    for (size_t i = 0; i < party.GameCount(); ++i)
        Talk(walk.Game(i)).GreetingInfo = kGreetingInfo;
    Talk(party.Host()).OnGreeting = [&] { cVoice(kGreetingInfo); };
    Talk(party.Host()).OnSaid = [&](const IDialogueBridge::Option& acSaid) {
        cVoice(acSaid.Info);
        if (acSaid.Topic != kMore.Topic)
            return;
        FakeEngine& host = Party::Engine(party.Host());
        auto& quest = host.Quests.at(kQuest);
        quest.CurrentStage += 10;
        quest.DoneStages.push_back(quest.CurrentStage);
        host.QuestChanged(kQuest);
    };
    party.As(party.Host(), [&] { walk.Npc = Party::Engine(party.Host()).LoadNpc(kNpc, kCell); });
    party.Until([&] {
        for (size_t i = 1; i < party.GameCount(); ++i)
            if (!walk.NpcIn(i))
                return false;
        return true;
    }, "every guest has the NPC");

    for (int step = 0; step < aSteps; ++step)
    {
        walk.Step();
        if (step % 10 == 9)
            if (const std::string cBroken = walk.Broken(); !cBroken.empty())
                FAIL("seed " << aSeed << ", after step " << step + 1 << ": " << cBroken << walk.Actions());
    }

    // Everyone leaves, pressing Tab again on a menu that opens late (an ask in flight): no menu stays open anywhere
    // and the host's conversation ends.
    const auto cState = [&] {
        std::string text;
        for (size_t i = 0; i < party.GameCount(); ++i)
        {
            const FakeDialogue& talk = Talk(walk.Game(i));
            const auto& cDialogue = walk.Game(i).ctx().at<DialogueService>();
            text += std::format("{}: menu {}, speaker {:X}, conversation {}, joined {}; ", walk.Name(i), talk.MenuOpen, talk.Speaker,
                                cDialogue.Conversation(), cDialogue.Joined());
        }
        return text;
    };
    if (std::getenv("DIALOGUE_WALK_TRACE"))
        WARN("before leaving: " << cState());
    party.Until([&] {
        bool open = false;
        for (size_t i = 0; i < party.GameCount(); ++i)
            if (FakeDialogue& talk = Talk(walk.Game(i)); talk.MenuOpen)
            {
                open = true;
                party.As(walk.Game(i), [&] { talk.PlayerLeaves(); });
            }
        return !open && !Talk(party.Host()).Speaker;
    }, std::format("seed {}: every player left, the conversation ended", aSeed), [&] { return cState() + walk.Actions(); });
    if (const std::string cBroken = walk.Broken(); !cBroken.empty())
        FAIL("seed " << aSeed << ", at the end: " << cBroken << walk.Actions());
}
} // namespace

// Seeds in check.sh; DIALOGUE_WALK_SEEDS=N runs N more from DIALOGUE_WALK_FROM.
SCENARIO("The dialogue walker: random players' actions on one conversation keep its rules", "[dialogue][dialogue-walk]")
{
    const char* cpSeeds = std::getenv("DIALOGUE_WALK_SEEDS");
    const char* cpFrom = std::getenv("DIALOGUE_WALK_FROM");
    const uint32_t cCount = cpSeeds ? static_cast<uint32_t>(std::atoi(cpSeeds)) : 6;
    const uint32_t cFrom = cpFrom ? static_cast<uint32_t>(std::atoi(cpFrom)) : 1;
    for (uint32_t seed = cFrom; seed < cFrom + cCount; ++seed)
    {
        CAPTURE(seed);
        const char* cpSteps = std::getenv("DIALOGUE_WALK_STEPS");
        RunWalk(seed, cpSteps ? std::atoi(cpSteps) : 80, seed % 2 ? 1 : 2);
    }
}

// dialogue-walk seed 198: a second ask replaced the first at the host, whose game refused the second: the conversation
// opened with nobody listed, the first asker's menu open on it, and nothing ended it.
SCENARIO("Two guests asking for the same NPC at once are both in its conversation, which ends when they leave", "[dialogue][dialogue-race]")
{
    Party party(2, SmokeWorld);
    for (World* pGame : {&party.Host(), &party.Guest(0), &party.Guest(1)})
        Script(Talk(*pGame));
    party.As(party.Host(), [&] { Party::Engine(party.Host()).LoadNpc(kNpc, kCell); });
    party.Until([&] { return !Party::Engine(party.Guest(0)).CopiesOf(kNpc).empty() && !Party::Engine(party.Guest(1)).CopiesOf(kNpc).empty(); },
                "both guests have the NPC");
    for (size_t g = 0; g < 2; ++g)
        party.As(party.Guest(g), [&] { Talk(party.Guest(g)).PlayerTalksTo(Party::Engine(party.Guest(g)).CopiesOf(kNpc)[0]); });
    party.Until([&] { return Talk(party.Guest(0)).ListShown && Talk(party.Guest(1)).ListShown; }, "both menus show the list");

    party.As(party.Guest(0), [&] { Talk(party.Guest(0)).PlayerLeaves(); });
    Settle(party);
    THEN("one leaving leaves it on for the other") CHECK(Talk(party.Host()).Speaker);
    party.As(party.Guest(1), [&] { Talk(party.Guest(1)).PlayerLeaves(); });
    party.Until([&] { return !Talk(party.Host()).Speaker; }, "the last one out ends it on the host's game");
}

SCENARIO("A guest pressing talk twice before the host answers is in the conversation it opens", "[dialogue][dialogue-race]")
{
    Party party(1, SmokeWorld);
    for (World* pGame : {&party.Host(), &party.Guest()})
        Script(Talk(*pGame));
    party.As(party.Host(), [&] { Party::Engine(party.Host()).LoadNpc(kNpc, kCell); });
    party.Until([&] { return !Party::Engine(party.Guest()).CopiesOf(kNpc).empty(); }, "the guest has the NPC");
    const uint32_t cCopy = Party::Engine(party.Guest()).CopiesOf(kNpc)[0];
    for (int i = 0; i < 2; ++i)
        party.As(party.Guest(), [&] { Talk(party.Guest()).PlayerTalksTo(cCopy); });
    party.Until([&] { return Talk(party.Guest()).ListShown; }, "its menu opens on the list",
                [&] { return std::format("guest's menu {}, host's speaker {:X}", Talk(party.Guest()).MenuOpen, Talk(party.Host()).Speaker); });
    party.As(party.Guest(), [&] { Talk(party.Guest()).PlayerLeaves(); });
    party.Until([&] { return !Talk(party.Host()).Speaker; }, "its leaving ends it on the host's game");
}

// dialogue-walk seed 1016 (COOP_NET): a guest's ask crossed the host's own short conversation with the NPC; the guest
// took that open for the answer to its ask, and ignored the conversation its ask then opened, which listed it.
SCENARIO("A guest's ask that crosses the host's own conversation with the NPC opens its menu on the one it is in", "[dialogue][dialogue-race]")
{
    Party party(1, SmokeWorld, true, NetConditions{.DelayMs = 60});
    for (World* pGame : {&party.Host(), &party.Guest()})
        Script(Talk(*pGame));
    auto& hostTalk = Talk(party.Host());
    auto& guestTalk = Talk(party.Guest());
    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = Party::Engine(party.Host()).LoadNpc(kNpc, kCell); });
    party.Until([&] { return !Party::Engine(party.Guest()).CopiesOf(kNpc).empty(); }, "the guest has the NPC");
    const uint32_t cCopy = Party::Engine(party.Guest()).CopiesOf(kNpc)[0];

    // The guest asks; before its ask arrives, the host talks to the NPC and leaves.
    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(cCopy); });
    party.As(party.Host(), [&] { hostTalk.PlayerTalksTo(npc); });
    party.Step(party.Host());
    party.As(party.Host(), [&] { hostTalk.PlayerLeaves(); });
    party.Until([&] { return guestTalk.ListShown && party.Guest().ctx().at<DialogueService>().Joined(); },
                "the guest's menu opens on the conversation its ask is in",
                [&] { return std::format("guest's menu {}, conversation {}, host's {}, host's speaker {:X}", guestTalk.MenuOpen,
                                         party.Guest().ctx().at<DialogueService>().Conversation(),
                                         party.Host().ctx().at<DialogueService>().Conversation(), hostTalk.Speaker); });
    party.As(party.Guest(), [&] { guestTalk.PlayerLeaves(); });
    party.Until([&] { return !hostTalk.Speaker; }, "its leaving ends it on the host's game");
}

// dialogue-guest-rejoins-quickly: a guest talking again mid-answer got a menu in the SWF's SHOW_GREETING for the rest
// of the answer, where Tab only skips the line, and a guest's skip skips the NPC's line for everyone.
SCENARIO("A guest that talks again while the NPC answers leaves with Tab, skipping nobody's line", "[dialogue][dialogue-race]")
{
    Party party(1, SmokeWorld);
    for (World* pGame : {&party.Host(), &party.Guest()})
        Script(Talk(*pGame));
    auto& hostTalk = Talk(party.Host());
    auto& guestTalk = Talk(party.Guest());
    hostTalk.LinesTakeTime = true;
    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = Party::Engine(party.Host()).LoadNpc(kNpc, kCell); });
    party.Until([&] { return !Party::Engine(party.Guest()).CopiesOf(kNpc).empty(); }, "the guest has the NPC");
    const uint32_t cCopy = Party::Engine(party.Guest()).CopiesOf(kNpc)[0];
    party.As(party.Host(), [&] { hostTalk.PlayerTalksTo(npc); });
    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(cCopy); });
    party.Until([&] { return hostTalk.ListShown && guestTalk.ListShown; }, "both menus show the list");
    party.As(party.Host(), [&] { hostTalk.PlayerPicks(1); });
    party.Until([&] { return hostTalk.LinePending() && guestTalk.MenuState() == 2; }, "the NPC answers, every menu shows the pick");

    party.As(party.Guest(), [&] { guestTalk.PlayerLeaves(); });
    Settle(party);
    REQUIRE(!guestTalk.MenuOpen);
    party.As(party.Guest(), [&] { guestTalk.PlayerTalksTo(cCopy); });
    Settle(party);
    REQUIRE(guestTalk.MenuOpen);
    THEN("its menu shows the pick, as the others while the NPC answers") CHECK(guestTalk.MenuState() == 2);
    party.As(party.Guest(), [&] { guestTalk.PlayerLeaves(); });
    Settle(party);
    THEN("Tab closes it, and the NPC's line goes on")
    {
        CHECK(!guestTalk.MenuOpen);
        CHECK(hostTalk.Skips == 0);
        CHECK(hostTalk.LinePending());
    }
}
