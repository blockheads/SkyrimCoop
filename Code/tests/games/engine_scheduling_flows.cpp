#include "Party.h"
#include "Fixtures.h"

#include <Messages/NotifyPartyTravel.h>
#include <Messages/WorldState.h>

SCENARIO("Party travel loads exterior cells between frames", "[travel][engine-scheduling]")
{
    Party party(1, [](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.Exterior(0x3C, 0x10000, 2);
    });
    auto& guest = Party::Engine(party.Guest());
    guest.WorkerCellLoads.clear();
    NotifyPartyTravel move;
    move.WorldSpaceId = GameId{0, 0x3C};
    move.Position = {100.f, 200.f, 0.f};
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(move); });
    party.Until([&] { return guest.Actors.at(FakeEngine::kPlayer).WorldSpace == 0x3C; },
                "the guest follows into the exterior");
    CHECK(guest.WorkerCellLoads.empty());
}

SCENARIO("Host quest alias changes run between frames", "[world][engine-scheduling]")
{
    Party party(1, SmokeWorld);
    auto& guest = Party::Engine(party.Guest());
    QuestState quest;
    quest.Id = GameId{0, 0x36191};
    quest.Flags = 1;
    quest.Aliases = {{3, GameId{0, 0x1A677}, false}};
    NotifyQuests message;
    message.Quests = {quest};
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(message); });
    party.Until([&] { return guest.QuestAliasReferences[0x36191][3] == 0x1A677; },
                "the host's alias is filled");
    quest.Flags = 0;
    quest.Aliases.clear();
    message.Quests = {quest};
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(message); });
    party.Until([&] { return guest.QuestAliasReferences[0x36191].empty(); },
                "the host's stopped quest releases its aliases");
    CHECK(guest.WorkerQuestWrites.empty());
}

SCENARIO("A disconnected guest cancels queued quest changes", "[world][engine-scheduling]")
{
    Party party(1, SmokeWorld);
    auto& guest = Party::Engine(party.Guest());
    QuestState quest;
    quest.Id = GameId{0, 0x36191};
    quest.Flags = 1;
    quest.Aliases = {{3, GameId{0, 0x1A677}, false}};
    NotifyQuests message;
    message.Quests = {quest};
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(message); });
    party.Leave(party.Guest());
    party.Step(party.Guest());
    CHECK_FALSE(guest.Quests.contains(0x36191));
    CHECK(guest.QuestAliasReferences[0x36191].empty());
}
