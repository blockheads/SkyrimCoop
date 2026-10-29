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

SCENARIO("A player's skills are on its copies from their spawn", "[perks][values]")
{
    // Joining applies a character's skills before it connects, so no change of them is relayed: the spawn carries
    // them, or a host condition on the copy (a persuade's Speech) reads the copy's created base.
    constexpr uint32_t cSpeech = 17;
    Party party(2, [&](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x31D2E;
        aEngine.LoadCell(0x31D2E);
        if (aEngine.Name == "Lydia")
        {
            aEngine.Actors.at(FakeEngine::kPlayer).Values.ActorValuesList[cSpeech] = 62.f;
            aEngine.Actors.at(FakeEngine::kPlayer).Values.ActorMaxValuesList[cSpeech] = 60.f;
        }
    });
    auto& host = Party::Engine(party.Host());
    party.Agree("everyone's copies are present");
    uint32_t session = 0, cOnHost = 0;
    party.As(party.Guest(), [&] { session = *Utils::GetSessionIdByFormId(FakeEngine::kPlayer); });
    party.As(party.Host(), [&] { cOnHost = Utils::GetFormIdBySessionId(session); });
    const auto& cValues = host.Actors.at(cOnHost).Values;
    CHECK(cValues.ActorValuesList.at(cSpeech) == 62.f);
    CHECK(cValues.ActorMaxValuesList.at(cSpeech) == 60.f);
}

SCENARIO("A player's added spells follow its copies, initially and after changes", "[perks][magic]")
{
    // A host condition judged for a guest (a trainer's HasSpell) reads the copy's added spells.
    constexpr uint32_t cFirebolt = 0x12FD0, cMagelight = 0x43323;
    Party party(2, [&](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x31D2E;
        aEngine.LoadCell(0x31D2E);
        if (aEngine.Name == "Lydia")
            aEngine.AddedSpells[FakeEngine::kPlayer] = {cFirebolt};
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& watcher = Party::Engine(party.Guest(1));
    party.Agree("everyone's copies are present");
    uint32_t session = 0, cOnHost = 0, cOnWatcher = 0;
    party.As(party.Guest(), [&] { session = *Utils::GetSessionIdByFormId(FakeEngine::kPlayer); });
    party.As(party.Host(), [&] { cOnHost = Utils::GetFormIdBySessionId(session); });
    party.As(party.Guest(1), [&] { cOnWatcher = Utils::GetFormIdBySessionId(session); });
    const auto cBoth = [&](const std::set<uint32_t>& acSpells) {
        return host.AddedSpells[cOnHost] == acSpells && watcher.AddedSpells[cOnWatcher] == acSpells;
    };
    party.Until([&] { return cBoth({cFirebolt}); }, "the spawn carries the spell the guest knows");
    party.As(party.Guest(), [&] { guest.AddedSpells[FakeEngine::kPlayer].insert(cMagelight); });
    party.Until([&] { return cBoth({cFirebolt, cMagelight}); }, "a spell the guest learns reaches its copies");
    party.As(party.Guest(), [&] { guest.AddedSpells[FakeEngine::kPlayer].erase(cFirebolt); });
    party.Until([&] { return cBoth({cMagelight}); }, "a spell the guest forgets leaves its copies");
    CHECK(!host.AddedSpells[FakeEngine::kPlayer].contains(cMagelight));
}
