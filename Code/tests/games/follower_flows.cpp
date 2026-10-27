#include "FakeDialogue.h"
#include "Party.h"
#include <Events/ActorAddedEvent.h>
#include <Events/MenuEvent.h>
#include <Followers/Records.h>
#include <Services/SaveService.h>
#include <SessionIds.h>

namespace
{
constexpr uint32_t kCell = 0x31D2E;
constexpr uint32_t kLydia = 0xA2C8E;
constexpr uint32_t kPick = 0xA;
void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
    aEngine.LoadCell(kCell);
}
uint32_t Character(Party& aParty, World& aGame)
{
    uint32_t id{};
    aParty.As(aGame, [&] { id = Utils::GetSessionIdByFormId(FakeEngine::kPlayer).value_or(0); });
    REQUIRE(id);
    return id;
}
} // namespace

SCENARIO("A follower already in the host's loaded world survives hosting", "[followers][actors]")
{
    World world;
    SetCurrentWorld(&world);
    auto& engine = Party::Engine(world);
    Plugins(engine);
    const uint32_t cFollower = engine.LoadNpc(kLydia, kCell);
    engine.PartyFollowers.push_back({cFollower});
    world.GetDispatcher().trigger(ActorAddedEvent{cFollower});
    REQUIRE(world.GetHostService().StartHosting(0));
    world.GetTransport().ConnectLocal(*world.GetHostService().GetNetwork());
    const auto cUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!world.GetTransport().IsOnline() && std::chrono::steady_clock::now() < cUntil)
        world.Update();
    REQUIRE(world.GetTransport().IsOnline());
    CHECK(engine.Actors.contains(cFollower));
}

SCENARIO("Three players share a follower while its pathing anchor and trade inventory belong to their pickers",
         "[followers][items]")
{
    Party party(2, Plugins);
    party.Agree("all three players are present");
    auto& host = Party::Engine(party.Host());
    auto& first = Party::Engine(party.Guest());
    auto& second = Party::Engine(party.Guest(1));
    const uint32_t cFirst = Character(party, party.Guest());
    const uint32_t cSecond = Character(party, party.Guest(1));
    uint32_t npc{};
    party.As(party.Host(), [&] {
        npc = host.LoadNpc(kLydia, kCell);
        host.GetsItem(npc, kPick, 3);
        host.PartyFollowers.push_back({npc});
        host.FollowerOrders.push_back({npc, cFirst, Followers::Command::Recruit});
    });
    party.Until(
        [&] {
            return !host.FollowerTargets.empty() && host.FollowerTargets[0].Character == cFirst &&
                   first.CopiesOf(kLydia).size() == 1 && second.CopiesOf(kLydia).size() == 1;
        },
        "the first guest recruits and both guests see the host's follower");
    const uint32_t cCopy = second.CopiesOf(kLydia)[0];
    auto& dialogue = party.Guest(1).ctx().at<FakeDialogue&>();
    party.As(party.Guest(1), [&] { dialogue.PlayerTalksTo(cCopy); });
    party.Until([&] { return dialogue.MenuOpen; }, "the inventory picker starts in follower dialogue");
    party.As(party.Host(), [&] { host.FollowerOrders.push_back({npc, cSecond, Followers::Command::Trade}); });
    party.Until([&] { return second.FollowerTradeNpc == cCopy; }, "only the trade picker opens the follower inventory");
    CHECK_FALSE(dialogue.MenuOpen);
    CHECK(first.FollowerTradeNpc == 0);
    CHECK(host.FollowerTradeNpc == 0);
    CHECK(host.FollowerTargets[0].Character == cFirst);
    party.As(party.Guest(1), [&] {
        second.LosesItem(cCopy, kPick, 1);
        second.GetsItem(FakeEngine::kPlayer, kPick, 1);
    });
    party.Until([&] { return host.CountOf(npc, kPick) == 2; }, "the host's follower loses the traded item once");
    CHECK(second.CountOf(FakeEngine::kPlayer, kPick) == 1);
    CHECK(first.CountOf(FakeEngine::kPlayer, kPick) == 0);
    CHECK(host.CountOf(FakeEngine::kPlayer, kPick) == 0);
    party.As(party.Guest(1), [&] {
        second.FollowerTradeNpc = 0;
        party.Guest(1).GetRunner().Trigger(MenuEvent{"ContainerMenu", false});
    });
    party.As(party.Host(), [&] { host.FollowerOrders.push_back({npc, cSecond, Followers::Command::Follow}); });
    party.Until([&] { return host.FollowerTargets[0].Character == cSecond; },
                "a new Follow command switches the physical anchor");
    party.Agree("the party agrees on the follower and personal inventory after trading");
}

SCENARIO("A guest activates a follower copy and its owner's actor receives that activation once", "[followers][actors]")
{
    Party party(2, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& other = Party::Engine(party.Guest(1));
    uint32_t npc{};
    party.As(party.Host(), [&] { npc = host.LoadNpc(0x109487, kCell); });
    party.Until([&] { return guest.CopiesOf(0x109487).size() == 1 && other.CopiesOf(0x109487).size() == 1; },
                "both guests see the host's dog");
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, guest.CopiesOf(0x109487)[0]); });
    party.Until([&] { return !host.Activations.empty(); }, "the host's dog receives the guest's activation");
    CHECK(host.Activations.size() == 1);
    CHECK(host.Activations[0].first == npc);
    CHECK(other.Activations.empty());
}

SCENARIO("A saved temporary NPC discovered after a guest connects does not become a second follower",
         "[followers][actors]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t npc{};
    party.As(party.Host(), [&] { npc = host.LoadNpc(kLydia, kCell); });
    party.Until([&] { return guest.CopiesOf(kLydia).size() == 1; }, "the guest sees its follower copy");
    uint32_t saved{};
    party.As(party.Guest(), [&] { saved = guest.LoadNpc(kLydia, kCell); });
    party.Until([&] { return !guest.Actors.contains(saved); }, "late discovery discards the guest's unowned saved NPC");
    CHECK(host.Actors.contains(npc));
    CHECK(guest.CopiesOf(kLydia).size() == 1);
    party.Agree("the guest has only the host's follower copy");
}
