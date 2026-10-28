#include "Party.h"

#include <Events/PlayerProgressEvent.h>
#include <SessionIds.h>

SCENARIO("A player's earned perks follow only its copies, initially and after changes", "[perks]")
{
    const std::vector<PlayerRecord::Perk> cFirst{{GameId(0, 0xCC799), 1}};
    const std::vector<PlayerRecord::Perk> cSecond{{GameId(0, 0xCC799), 2}};
    Party party(2, [&](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x31D2E;
        aEngine.LoadCell(0x31D2E);
        if (aEngine.Name == "Lydia")
            aEngine.Player.Perks = cFirst;
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& watcher = Party::Engine(party.Guest(1));
    party.Agree("everyone's copies are present");
    uint32_t session = 0, cOnHost = 0, cOnWatcher = 0;
    party.As(party.Guest(), [&] { session = *Utils::GetSessionIdByFormId(FakeEngine::kPlayer); });
    party.As(party.Host(), [&] { cOnHost = Utils::GetFormIdBySessionId(session); });
    party.As(party.Guest(1), [&] { cOnWatcher = Utils::GetFormIdBySessionId(session); });
    party.Until([&] { return host.ActorPerks[cOnHost] == cFirst && watcher.ActorPerks[cOnWatcher] == cFirst; },
                "the initial perk rank is on both copies");
    CHECK(host.Player.Perks.empty());
    CHECK(watcher.Player.Perks.empty());
    party.As(party.Guest(), [&] {
        guest.Player.Perks = cSecond;
        party.Guest().GetDispatcher().trigger(PlayerProgressEvent{});
    });
    party.Until([&] { return host.ActorPerks[cOnHost] == cSecond && watcher.ActorPerks[cOnWatcher] == cSecond; },
                "a changed rank reaches the copies");
    const bool cQueued = GENERATE(false, true);
    if (cQueued)
    {
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(PlayerProgressEvent{}); });
        party.Frame();
    }
    party.As(party.Guest(), [&] {
        guest.Player.Perks.clear();
        if (!cQueued)
            party.Guest().GetDispatcher().trigger(PlayerProgressEvent{});
    });
    party.Until([&] { return host.ActorPerks[cOnHost].empty() && watcher.ActorPerks[cOnWatcher].empty(); },
                "removed perks leave every copy");
}
