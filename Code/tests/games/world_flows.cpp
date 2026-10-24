// World-state sync across games (docs/design/world-state-sync.md): the host's world decides; a guest's player's
// changes are proposals the host's world takes; a cell a guest loads gets the host's state there. The checksum is
// Party::Differences: every reference in a cell two games have loaded is the same.
#include "Party.h"

#include <Events/LockChangeEvent.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;         // a cell everyone stands in
constexpr uint32_t kSinkhole = 0x0009C5E1;      // another cell
constexpr uint32_t kDoor = 0x0001A002;          // a locked door in kSmoke (level 25)
constexpr uint32_t kGate = 0x0001A003;          // another locked door there
constexpr uint32_t kChest = 0x0001A001;         // a chest in kSmoke: 50 gold
constexpr uint32_t kSinkholeChest = 0x0001B001; // a chest in kSinkhole: 10 gold
constexpr uint32_t kGold = 0x0000000F;
constexpr uint32_t kInnDoor = 0x0001A004;    // an unlocked door in kSmoke
constexpr uint32_t kTimesAsked = 0x0003F1D6; // a global the host's quests count with

// Each game's plugins: the same references; everyone has kSmoke loaded.
void Plugins(FakeEngine& aEngine)
{
    aEngine.References[kDoor] = {.Cell = kSmoke, .Locked = true, .LockLevel = 25};
    aEngine.References[kGate] = {.Cell = kSmoke, .Locked = true, .LockLevel = 50};
    aEngine.References[kChest] = {.Cell = kSmoke, .Items = {{kGold, 50}}};
    aEngine.References[kSinkholeChest] = {.Cell = kSinkhole, .Items = {{kGold, 10}}};
    aEngine.References[kInnDoor] = {.Cell = kSmoke, .Door = true};
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

void CheckInSync(Party& aParty)
{
    const auto cDifferences = aParty.Differences();
    for (const auto& cDifference : cDifferences)
        UNSCOPED_INFO(cDifference);
    CHECK(cDifferences.empty());
}
} // namespace

SCENARIO("The host's player picks a lock: it opens on the guest's game", "[world]")
{
    Party party(1, Plugins);
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlayerSetsLock(kDoor, false, 25); });
    party.Until([&] { return !Party::Engine(party.Guest()).References[kDoor].Locked; }, "the guest's door unlocks");
    CheckInSync(party);
}

SCENARIO("A guest picks a lock: the host's world takes it, and it comes back as the host's", "[world]")
{
    Party party(1, Plugins);
    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).PlayerSetsLock(kDoor, false, 25); });
    party.Until([&] { return !Party::Engine(party.Host()).References[kDoor].Locked; }, "the host's door unlocks");
    THEN("the host's game recorded it as its own change") CHECK(Party::Engine(party.Host()).Changed.contains(kDoor));
    CheckInSync(party);
}

SCENARIO("A guest's world changing a lock is no proposal", "[world]")
{
    Party party(1, Plugins);
    WHEN("the guest's world (a script, a cell load) locks the door, then its player picks the gate")
    {
        party.As(party.Guest(),
                 [&] { party.Guest().GetDispatcher().trigger(LockChangeEvent(kDoor, false, 0, false)); });
        party.As(party.Guest(), [&] { Party::Engine(party.Guest()).PlayerSetsLock(kGate, false, 50); });
        party.Until([&] { return !Party::Engine(party.Host()).References[kGate].Locked; }, "the host's gate unlocks");
        THEN("the host's door stays locked") CHECK(Party::Engine(party.Host()).References[kDoor].Locked);
    }
}

SCENARIO("A guest takes gold from a chest: the host's chest holds what the guest's does", "[world]")
{
    Party party(1, Plugins);
    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).PlayerSetsItems(kChest, kGold, 40); });
    party.Until([&] { return Party::Engine(party.Host()).References[kChest].Items[kGold] == 40; },
                "the host's chest holds 40");
    CheckInSync(party);
}

SCENARIO("A cell a guest never loaded: the host's changes there arrive when it loads it", "[world]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    party.As(party.Host(), [&] {
        host.LoadCell(kSinkhole);
        host.PlayerSetsItems(kSinkholeChest, kGold, 3);
    });
    // A marker after it: the guest has had every change the host sent before.
    party.As(party.Host(), [&] { host.PlayerSetsLock(kGate, false, 50); });
    party.Until([&] { return !guest.References[kGate].Locked; }, "the marker reaches the guest");
    REQUIRE(guest.References[kSinkholeChest].Items[kGold] == 10); // not in memory there: not applied

    WHEN("the guest loads the cell")
    {
        guest.LoadCell(kSinkhole);
        party.Until([&] { return guest.References[kSinkholeChest].Items[kGold] == 3; },
                    "the cell's state reaches the guest");
        CheckInSync(party);
    }
}

SCENARIO("A guest away while the host changes things gets them when it joins again", "[world]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    party.Leave(party.Guest());
    party.As(party.Host(), [&] {
        host.PlayerSetsLock(kDoor, false, 25);
        host.PlayerSetsItems(kChest, kGold, 7);
    });
    party.Frame();
    REQUIRE(guest.References[kDoor].Locked);

    WHEN("it joins again, its loaded cells ask for the host's state")
    {
        party.Join(party.Guest());
        party.Until([&] { return !guest.References[kDoor].Locked && guest.References[kChest].Items[kGold] == 7; },
                    "the host's changes reach the rejoined guest");
        CheckInSync(party);
    }
}

SCENARIO("Two guests: one's proposal reaches the other through the host", "[world]")
{
    Party party(2, Plugins);
    party.As(party.Guest(0), [&] { Party::Engine(party.Guest(0)).PlayerSetsItems(kChest, kGold, 25); });
    party.Until([&] { return Party::Engine(party.Guest(1)).References[kChest].Items[kGold] == 25; },
                "the other guest's chest holds 25");
    CheckInSync(party);
}

SCENARIO("A guest opens a door: it opens on the host's game, and the host's closing reaches the guest", "[world]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return !host.CopiesOf().empty(); }, "the host's game has the guest's player");

    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, kInnDoor); });
    party.Until([&] { return host.References.at(kInnDoor).Open; }, "the door opens on the host's game");

    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, kInnDoor); });
    party.Until([&] { return !guest.References.at(kInnDoor).Open; }, "the host's closing it reaches the guest");
    CheckInSync(party);
}

SCENARIO("The host's globals reach the guests: at the join and as they change", "[world]")
{
    Party party(1, Plugins, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    host.SetGlobal(kTimesAsked, 3.f);

    party.Join(party.Guest());
    party.Until([&] { return guest.Globals[kTimesAsked] == 3.f; }, "a joining guest gets the host's global");
    host.SetGlobal(kTimesAsked, 7.f);
    party.Until([&] { return guest.Globals[kTimesAsked] == 7.f; }, "the host's change reaches it");
}

SCENARIO("Quest writes reach guests whole and coalesced on the next frame", "[world][quests]")
{
    constexpr uint32_t kQuest = 0x36191;
    Party party(1, [=](FakeEngine& aEngine) {
        Plugins(aEngine);
        QuestState quest;
        quest.Id = GameId{0, kQuest};
        aEngine.Quests[kQuest] = quest;
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& quest = host.Quests.at(kQuest);
    quest.Flags = 1;
    quest.CurrentStage = 20;
    quest.DoneStages = {10, 20};
    host.QuestChanged(kQuest);
    quest.Objectives = {{20, 1}};
    host.QuestChanged(kQuest);
    quest.Aliases = {{3, GameId{0, 0x1A677}, false}};
    quest.Locations = {{4, GameId{0, 0x18A56}}};
    host.QuestChanged(kQuest);
    party.Until([&] { return guest.Quests.at(kQuest) == quest; }, "the stage, objectives and alias fills reach the guest");

    quest.Flags = 0x42;
    quest.Objectives.clear();
    quest.Aliases.clear();
    quest.Locations.clear();
    host.QuestChanged(kQuest);
    party.Until([&] { return guest.Quests.at(kQuest) == quest; }, "completion, failure and cleared aliases reach the guest");
}
