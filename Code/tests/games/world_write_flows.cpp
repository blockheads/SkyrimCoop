#include "Party.h"

namespace
{
constexpr uint32_t kSmoke = 0x31D2E;
constexpr uint32_t kQuest = 0x36191;
constexpr uint32_t kWerewolfPerks = 0x10001;
constexpr uint32_t kVampirePerks = 0x10002;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

QuestState FilledQuest()
{
    QuestState quest;
    quest.Id = GameId{0, kQuest};
    quest.Flags = 1;
    quest.CurrentStage = 20;
    quest.DoneStages = {10, 20};
    quest.Objectives = {{20, 1}};
    quest.Aliases = {{3, GameId{0, 0x1A677}, false, GameId{0, 0x3C}, 2, -3},
                     {4, GameId{0, 0x1A678}, false, GameId{0, kSmoke}}};
    quest.Locations = {{5, GameId{0, 0x18A56}}, {6, GameId{0, 0x18A57}}};
    return quest;
}

struct QuestLog
{
    void Receive(const NotifyQuests& acMessage)
    {
        Messages.push_back(acMessage);
    }
    std::vector<NotifyQuests> Messages;
};

void ClearAlias(bool aLocation)
{
    size_t games = 0;
    const QuestState cInitial = FilledQuest();
    Party party(1, [&](FakeEngine& aEngine) {
        Plugins(aEngine);
        auto quest = cInitial;
        if (games++ != 0)
        {
            quest.Aliases = {{3}, {4}};
            quest.Locations = {{5}, {6}};
        }
        aEngine.Quests[kQuest] = quest;
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.Quests.at(kQuest) == cInitial; }, "the guest first has the filled quest");
    QuestLog log;
    entt::scoped_connection connection =
        party.Guest().GetDispatcher().sink<NotifyQuests>().connect<&QuestLog::Receive>(&log);
    auto expected = cInitial;
    if (aLocation)
        expected.Locations[0] = QuestLocationFill{5};
    else
        expected.Aliases[0] = QuestAliasFill{3};
    party.As(party.Host(), [&] {
        if (aLocation)
            host.ClearLocationAlias(kQuest, 5);
        else
            host.ClearReferenceAlias(kQuest, 3);
    });
    REQUIRE(host.Quests.at(kQuest) == expected);
    party.Until([&] { return guest.Quests.at(kQuest) == expected; },
                aLocation ? "the guest drops only the location fill"
                          : "the guest drops only the reference fill and placement");
    REQUIRE_FALSE(log.Messages.empty());
    CHECK_FALSE(log.Messages.back().Join);
    REQUIRE(log.Messages.back().Quests.size() == 1);
    CHECK(log.Messages.back().Quests.front() == expected);
    CHECK(guest.Quests.at(kQuest).Flags == cInitial.Flags);
    CHECK(guest.Quests.at(kQuest).CurrentStage == cInitial.CurrentStage);
}

struct GlobalLog
{
    void Receive(const NotifyGlobals& acMessage)
    {
        Messages.push_back(acMessage);
    }
    std::vector<NotifyGlobals> Messages;
};
} // namespace

SCENARIO("A reference alias clear alone reaches the guest", "[world][write-events][alias-clear]")
{
    ClearAlias(false);
}

SCENARIO("A created actor's quest role waits for its copy, then clears when the host clears it", "[world][actor-alias]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t actor = 0;
    party.As(party.Host(), [&] { actor = host.LoadNpc(0x1A67C, kSmoke); });
    party.Step(party.Host());
    uint32_t session = 0;
    for (auto&& [cEntity, form, own] : party.Host().view<FormIdComponent, LocalComponent>().each())
        if (form.Id == actor)
            session = own.Id;
    REQUIRE(session);
    NotifyQuests update;
    auto& quest = update.Quests.emplace_back();
    quest.Id = GameId(0, kQuest);
    quest.Flags = 1;
    quest.Aliases = {{.AliasId = 17, .ActorId = session}};
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(update); });
    CHECK(guest.QuestAliasReferences[kQuest][17] == 0);
    party.Until([&] {
        return guest.CopiesOf(0x1A67C).size() == 1 &&
               guest.QuestAliasReferences[kQuest][17] == guest.CopiesOf(0x1A67C)[0];
    }, "the quest's role is filled after the actor's copy arrives");
    quest.Aliases = {{17}};
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(update); });
    party.Frame();
    CHECK(guest.QuestAliasReferences[kQuest][17] == 0);
}

SCENARIO("A location alias clear alone reaches the guest", "[world][write-events][alias-clear]")
{
    ClearAlias(true);
}

SCENARIO("Beast perk refunds reach the guest down to zero", "[world][write-events][beast-refund]")
{
    bool vampire = false;
    SECTION("werewolf")
    {
        vampire = false;
    }
    SECTION("vampire")
    {
        vampire = true;
    }
    size_t games = 0;
    Party party(1, [&](FakeEngine& aEngine) {
        Plugins(aEngine);
        const bool cHost = games++ == 0;
        aEngine.BeastPerkGlobals = {kWerewolfPerks, kVampirePerks};
        aEngine.Globals[kWerewolfPerks] = cHost ? 8.f : 0.f;
        aEngine.Globals[kVampirePerks] = cHost ? 12.f : 0.f;
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.Globals.at(kWerewolfPerks) == 8.f && guest.Globals.at(kVampirePerks) == 12.f; },
                "the guest first has both perk globals");
    const uint32_t cSelected = vampire ? kVampirePerks : kWerewolfPerks;
    const uint32_t cOther = vampire ? kWerewolfPerks : kVampirePerks;
    const float cUnchanged = guest.Globals.at(cOther);
    const float cRefunded = host.Globals.at(cSelected) - 2.f;
    GlobalLog log;
    entt::scoped_connection connection =
        party.Guest().GetDispatcher().sink<NotifyGlobals>().connect<&GlobalLog::Receive>(&log);
    party.As(party.Host(), [&] { host.ModifyBeastPerks(vampire, -2); });
    party.Until([&] { return guest.Globals.at(cSelected) == cRefunded; }, "the guest's selected perk global decreases");
    REQUIRE(log.Messages.size() == 1);
    REQUIRE(log.Messages.front().Values.size() == 1);
    CHECK(log.Messages.front().Values.front().Id == GameId{0, cSelected});
    CHECK(log.Messages.front().Values.front().Value == cRefunded);
    CHECK(guest.Globals.at(cOther) == cUnchanged);
    party.As(party.Host(), [&] { host.ModifyBeastPerks(vampire, -static_cast<int32_t>(cRefunded)); });
    party.Until([&] { return guest.Globals.at(cSelected) == 0.f; }, "the final refund reaches zero on the guest");
    CHECK(guest.Globals.at(cOther) == cUnchanged);
}
