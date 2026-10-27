#include "FakeCrime.h"
#include "FakeDialogue.h"
#include "Party.h"
#include <Services/TransportService.h>

namespace
{
FakeCrime& Crime(World& aWorld)
{
    return aWorld.ctx().at<FakeCrime&>();
}
} // namespace

SCENARIO("Host bounty reaches every guest and corrects a local overwrite", "[crime]")
{
    Party party(2);
    auto& host = Crime(party.Host());
    host.Gold = {{GameId(0, 0x28170), 1000, 25, 25, 1000}, {GameId(0xFE, 0x100812), 0, 50, 50, 0}};
    party.Until([&] { return Crime(party.Guest()).Gold == host.Gold && Crime(party.Guest(1)).Gold == host.Gold; },
                "every guest receives the host's violent and nonviolent bounty");
    Crime(party.Guest()).Gold = {{GameId(0, 0x28170), 0, 9999}};
    party.Until([&] { return Crime(party.Guest()).Gold == host.Gold; },
                "a local native write cannot change party bounty");
    host.Gold.clear();
    party.Until([&] { return Crime(party.Guest()).Gold.empty() && Crime(party.Guest(1)).Gold.empty(); },
                "the native host's pardon clears every guest");
}

SCENARIO("Guest crime actions go to the host engine once and its judgment returns", "[crime]")
{
    Party party;
    auto& host = Crime(party.Host());
    auto& guest = Crime(party.Guest());
    bool seen = false;
    host.JudgeAction = [&](uint32_t, const CrimeRequest&) {
        if (seen)
            host.Gold = {{GameId(0, 0x28170), 0, 37}};
        return false;
    };
    CrimeRequest theft;
    theft.Action = CrimeAction::Theft;
    theft.Item = GameId(0, 0xF);
    theft.Target = GameId(0, 0x1234);
    theft.Count = 37;
    guest.Actions.push_back(theft);
    party.Until([&] { return host.Judged.size() == 1; }, "the host engine receives the unseen crime proposal");
    CHECK(host.Gold.empty());
    CHECK(guest.Gold.empty());
    CHECK(host.Judged.front().first != FakeEngine::kPlayer); // the guest's actual copy, never the host's body
    seen = true;
    guest.Actions.push_back(theft);
    party.Until([&] { return !guest.Gold.empty(); }, "the host's witnessed result reaches the guest");
    CHECK(host.Judged.size() == 2);
    CHECK(guest.Gold == host.Gold);
}

SCENARIO("A reconnecting guest replaces stale bounty and receives its own lock decision", "[crime]")
{
    Party party(2);
    auto& host = Crime(party.Host());
    auto& guest = Crime(party.Guest());
    party.As(party.Guest(), [&] { party.Guest().GetTransport().Close(); });
    host.Gold = {{GameId(0, 0x28170), 55, 11, 0, 0, true}};
    guest.Gold = {{GameId(0, 0x28170), 1, 2}};
    party.Join(party.Guest());
    party.Until([&] { return guest.Gold == host.Gold; }, "the rejoining game receives authoritative bounty");
    host.JudgeAction = [](uint32_t, const CrimeRequest&) { return true; };
    CrimeRequest lock;
    lock.Action = CrimeAction::Lockpick;
    lock.Target = GameId(0, 0x1234);
    lock.Count = 81;
    guest.Actions.push_back(lock);
    party.Until([&] { return guest.Caught == 81; }, "the lockpicker receives the host witness decision");
    CHECK(Crime(party.Guest(1)).Caught == 0);
}

SCENARIO("Confiscation runs once on the chooser's game with its own stolen-item ownership", "[crime]")
{
    Party party(2, [](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x31D2E;
        aEngine.LoadCell(0x31D2E);
    });
    party.Agree("the payer's copy exists on the host");
    auto& engine = Party::Engine(party.Host());
    auto& host = Crime(party.Host());
    uint32_t payer{};
    for (const auto& [id, actor] : engine.Actors)
        if (actor.Player && actor.Copy) { payer = id; break; }
    REQUIRE(payer);
    host.Confiscations.push_back({payer, GameId(0, 0x28170)});
    party.Until([&] { return Crime(party.Guest()).Confiscated.size() + Crime(party.Guest(1)).Confiscated.size() == 1; },
                "only the payer's game confiscates its stolen stacks");
    CHECK(host.Confiscated.empty());
    party.As(party.Host(), [&] { CHECK(host.TakeConfiscations().empty()); });
    auto& chooser = Crime(party.Guest()).Confiscated.empty() ? party.Guest(1) : party.Guest();
    NotifyCrimeConfiscation replay;
    replay.Id = 1;
    replay.Faction = GameId(0, 0x28170);
    party.As(chooser, [&] {
        chooser.GetDispatcher().trigger(replay);
        chooser.GetDispatcher().trigger(replay);
    });
    CHECK(Crime(chooser).Confiscated.size() == 1);
}

SCENARIO("Criminal flags reach the whole party and the host maintains only player-copy crime state", "[crime]")
{
    Party party(2, [](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x31D2E;
        aEngine.LoadCell(0x31D2E);
    });
    party.Agree("the party's copies exist");
    auto& host = Crime(party.Host());
    host.Gold = {{GameId(0, 0x267EA), 0, 0, 0, 0, true}};
    party.Until([&] { return Crime(party.Guest()).Gold == host.Gold && Crime(party.Guest(1)).Gold == host.Gold; },
                "native criminal flags are shared even without a monetary bounty");
    REQUIRE(host.CrimePlayers.size() == 2);
    for (auto id : host.CrimePlayers)
    {
        const auto& actor = Party::Engine(party.Host()).Actors.at(id);
        CHECK(actor.Copy);
        CHECK(actor.Player);
    }
    CHECK(Crime(party.Guest()).CrimePlayers.empty());
    CHECK(Crime(party.Guest(1)).CrimePlayers.empty());
    Crime(party.Guest()).Gold.front().PlayerEnemy = false;
    party.Until([&] { return Crime(party.Guest()).Gold == host.Gold; }, "a local guest cannot clear party hostility");
    host.Gold.front().PlayerEnemy = false;
    party.Until([&] { return Crime(party.Guest()).Gold == host.Gold && Crime(party.Guest(1)).Gold == host.Gold; },
                "a host pardon clears the same flag in both guest games");
}

SCENARIO("A guard's arrest greeting reaches both guests and their jail choice tethers the party", "[crime][dialogue]")
{
    constexpr uint32_t smoke = 0x31D2E, jail = 0x30EF9, guardBase = 0x267EE, greeting = 0x2A37C;
    const IDialogueBridge::Option submit{.Topic = 0x267DD, .Info = 0xAD7C7, .Text = "I submit. Take me to jail."};
    Party party(2, [](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = smoke;
        aEngine.LoadCell(smoke);
    });
    auto& host = Party::Engine(party.Host());
    auto& hostTalk = party.Host().ctx().at<FakeDialogue&>();
    hostTalk.Greeting = {submit};
    hostTalk.GreetingInfo = greeting;
    uint32_t guard{};
    party.As(party.Host(), [&] { guard = host.LoadNpc(guardBase, smoke); });
    party.Until([&] { return !Party::Engine(party.Guest()).CopiesOf(guardBase).empty()
                         && !Party::Engine(party.Guest(1)).CopiesOf(guardBase).empty(); }, "both guests have the guard");
    for (size_t i = 0; i < 2; ++i)
        party.Guest(i).ctx().at<FakeDialogue&>().RequiresGreeting = true;
    uint32_t sentences{};
    hostTalk.OnSaid = [&](const auto&) {
        ++sentences;
        host.PlayerTravels(jail, {50.f, 60.f, 70.f}); // native GoToPrison's arrival, not another crime transport
    };
    party.As(party.Host(), [&] { hostTalk.NpcGreets(guard); });
    party.Until([&] {
        for (size_t i = 0; i < 2; ++i)
        {
            const auto& talk = party.Guest(i).ctx().at<FakeDialogue&>();
            if (!talk.MenuOpen || talk.Options != std::vector{submit})
                return false;
        }
        return true;
    }, "every guest opens the native arrest greeting with the host's choices");
    for (size_t i = 0; i < 2; ++i)
        CHECK(party.Guest(i).ctx().at<FakeDialogue&>().StartedGreeting == greeting);
    const IDialogueBridge::Option fine{.Topic = 0x267DB, .Info = 0x267E0, .Text = "Pay the bounty. (50 gold)"};
    const std::vector choices{fine, submit};
    party.As(party.Host(), [&] { hostTalk.RebuildOptions(choices); });
    party.Until([&] {
        return party.Guest().ctx().at<FakeDialogue&>().Options == choices
            && party.Guest(1).ctx().at<FakeDialogue&>().Options == choices;
    }, "a native arrest-list rebuild reaches both guests before anyone picks");
    party.As(party.Guest(), [&] { party.Guest().ctx().at<FakeDialogue&>().PlayerPicks(1); });
    party.Until([&] {
        for (auto* game : {&party.Host(), &party.Guest(), &party.Guest(1)})
            if (Party::Engine(*game).Actors.at(FakeEngine::kPlayer).Cell != jail)
                return false;
        return true;
    }, "one jail sentence brings the host and both guests into the same cell");
    CHECK(sentences == 1);
    CHECK(hostTalk.Said == std::vector{submit});
    for (size_t i = 0; i < 2; ++i)
    {
        CHECK(party.Guest(i).ctx().at<FakeDialogue&>().Said.empty());
        const auto& player = Party::Engine(party.Guest(i)).Actors.at(FakeEngine::kPlayer);
        CHECK(player.Position[0] == 50.f);
        CHECK(player.Position[1] == 60.f);
        CHECK(player.Position[2] == 70.f);
    }
    party.Agree("the three games agree on the jailed party");
}
