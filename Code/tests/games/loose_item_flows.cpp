// Dropped items (docs/design/dropped-items.md): a loose item exists once in the party's world, the host's, with its
// extra data; whoever picks it up gets it and it is gone for everyone. Chests keep their items' extra data too.
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;   // a cell everyone stands in
constexpr uint32_t kSinkhole = 0x0009C5E1; // a cell only a guest has loaded
constexpr uint32_t kChest = 0x0001A001;   // a chest in kSmoke
constexpr uint32_t kIronSword = 0x00012EB7;

void Plugins(FakeEngine& aEngine)
{
    aEngine.References[kChest] = {.Cell = kSmoke};
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

// An enchanted, tempered, renamed sword.
Inventory::Entry Heirloom()
{
    Inventory::Entry sword;
    sword.BaseId = SessionIdOf(kIronSword);
    sword.Count = 1;
    sword.ExtraEnchantId = GameId(0, 0x4605A);
    sword.ExtraEnchantCharge = 1000;
    sword.ExtraCharge = 800.f;
    sword.ExtraHealth = 1.4f;
    sword.ExtraName = "Old Friend";
    return sword;
}

// The loose items of that game holding the item.
std::vector<uint32_t> LooseOf(const FakeEngine& acEngine, const Inventory::Entry& acItem)
{
    std::vector<uint32_t> found;
    for (const auto& [cId, cLoose] : acEngine.Loose)
        if (cLoose.Item == acItem)
            found.push_back(cId);
    return found;
}

// How many of that sword, extra data and all, every player of the party carries.
int32_t CarriedByAll(Party& aParty, size_t aGuests, const Inventory::Entry& acItem)
{
    int32_t count = 0;
    const auto cCount = [&](World& aGame) {
        for (const auto& cEntry : Party::Engine(aGame).EntriesOf(FakeEngine::kPlayer, kIronSword))
            if (cEntry.CanBeMerged(acItem))
                count += cEntry.Count;
    };
    cCount(aParty.Host());
    for (size_t i = 0; i < aGuests; ++i)
        cCount(aParty.Guest(i));
    return count;
}

// Frames for a while: what has not happened by then (a second grant, a duplicate) does not.
void Settle(Party& aParty)
{
    const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    aParty.Until([&] { return std::chrono::steady_clock::now() > cUntil; }, "a while passes");
}

// That game's copies of other games' players.

} // namespace

SCENARIO("The host's player drops an heirloom: the guest sees it with its extra data, picks it up, it is gone for everyone", "[items][loose]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopies(host).size() == 1; }, "the host's game has the guest's player");

    party.As(party.Host(), [&] { host.GetsItem(FakeEngine::kPlayer, Heirloom()); });
    uint32_t dropped = 0;
    party.As(party.Host(), [&] { dropped = host.DropsInWorld(Heirloom()); });
    party.Until([&] { return LooseOf(guest, Heirloom()).size() == 1; }, "the guest's world shows the heirloom, extra data and all");
    CHECK(host.EntriesOf(FakeEngine::kPlayer, kIronSword).empty());

    const uint32_t cTwin = LooseOf(guest, Heirloom())[0];
    party.As(party.Guest(), [&] { guest.PicksUp(cTwin); });
    THEN("the guest's game waits for the host's answer") CHECK(guest.Loose.contains(cTwin));
    party.Until([&] { return !host.Loose.contains(dropped) && guest.Loose.empty(); }, "the heirloom leaves both worlds");
    party.Until([&] { return CarriedByAll(party, 1, Heirloom()) == 1; }, "the guest's player carries it");
    CHECK(guest.EntriesOf(FakeEngine::kPlayer, kIronSword).size() == 1);
    party.Until([&] { return host.CountOf(PlayerCopies(host)[0], kIronSword) == 1; }, "the host's copy of the guest carries it");
}

SCENARIO("A guest drops an heirloom: the host's world makes it, the dropper's own is its twin, the host picks it up", "[items][loose]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& dropper = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));

    party.As(party.Guest(0), [&] { dropper.GetsItem(FakeEngine::kPlayer, Heirloom()); });
    uint32_t dropped = 0;
    party.As(party.Guest(0), [&] { dropped = dropper.DropsInWorld(Heirloom()); });
    party.Until([&] { return LooseOf(host, Heirloom()).size() == 1 && LooseOf(other, Heirloom()).size() == 1; },
                "the host's world and the other guest's show it");
    // The host's creation came back to the dropper too: it is its own drop, not a second one.
    Settle(party);
    THEN("the dropper still has the one it dropped") CHECK(dropper.Loose.size() == 1);
    CHECK(dropper.Loose.contains(dropped));

    party.As(party.Host(), [&] { host.PicksUp(LooseOf(host, Heirloom())[0]); });
    party.Until([&] { return host.Loose.empty() && dropper.Loose.empty() && other.Loose.empty(); }, "it leaves every world");
    CHECK(CarriedByAll(party, 2, Heirloom()) == 1);
    CHECK(host.EntriesOf(FakeEngine::kPlayer, kIronSword).size() == 1);
}

SCENARIO("Two guests reach for one item: the first gets it, the other nothing", "[items][loose]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& first = Party::Engine(party.Guest(0));
    FakeEngine& second = Party::Engine(party.Guest(1));

    party.As(party.Host(), [&] {
        host.GetsItem(FakeEngine::kPlayer, Heirloom());
        host.DropsInWorld(Heirloom());
    });
    party.Until([&] { return LooseOf(first, Heirloom()).size() == 1 && LooseOf(second, Heirloom()).size() == 1; },
                "both guests see it");
    party.As(party.Guest(0), [&] { first.PicksUp(LooseOf(first, Heirloom())[0]); });
    party.As(party.Guest(1), [&] { second.PicksUp(LooseOf(second, Heirloom())[0]); });
    party.Until([&] { return host.Loose.empty() && first.Loose.empty() && second.Loose.empty(); }, "it leaves every world");
    party.Until([&] { return CarriedByAll(party, 2, Heirloom()) == 1; }, "one player carries it");
    Settle(party);
    THEN("and only one") CHECK(CarriedByAll(party, 2, Heirloom()) == 1);
}

SCENARIO("A guest's drop where the host's game has no cell loaded waits; picked up again, it comes back", "[items][loose]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    party.As(party.Guest(), [&] {
        guest.LoadCell(kSinkhole);
        guest.Actors.at(FakeEngine::kPlayer).Cell = kSinkhole;
        guest.GetsItem(FakeEngine::kPlayer, Heirloom());
    });
    uint32_t dropped = 0;
    party.As(party.Guest(), [&] { dropped = guest.DropsInWorld(Heirloom()); });
    Settle(party);
    CHECK(host.Loose.empty());

    party.As(party.Guest(), [&] { guest.PicksUp(dropped); });
    party.Until([&] { return guest.Loose.empty(); }, "the guest's player picks it up again");
    party.Until([&] { return CarriedByAll(party, 1, Heirloom()) == 1; }, "it carries it again");
    party.As(party.Host(), [&] { host.LoadCell(kSinkhole); });
    Settle(party);
    THEN("the host's game never makes it") CHECK(host.Loose.empty());
}

SCENARIO("A guest's drop the host's game cannot make stays its own, picked up without asking", "[items][loose]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    host.MissingForms.insert(kIronSword);

    party.As(party.Guest(), [&] { guest.GetsItem(FakeEngine::kPlayer, Heirloom()); });
    uint32_t dropped = 0;
    party.As(party.Guest(), [&] { dropped = guest.DropsInWorld(Heirloom()); });
    party.Until([&] { return guest.HostDecides.contains(dropped); }, "the guest's game asks the host to make its drop");
    party.Until([&] { return !guest.HostDecides.contains(dropped); }, "the host's answer reaches the guest");
    CHECK(host.Loose.empty());

    party.As(party.Guest(), [&] { guest.PicksUp(dropped); });
    THEN("its player picks it up at once") CHECK(!guest.Loose.contains(dropped));
    party.Until([&] { return CarriedByAll(party, 1, Heirloom()) == 1; }, "it carries it again");
}

SCENARIO("A chest keeps its items' extra data, the host's and a guest's", "[items][world]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    WHEN("the host's player stores the heirloom")
    {
        party.As(party.Host(), [&] { host.PlayerSetsItems(kChest, kIronSword, 1, {Heirloom()}); });
        party.Until([&] { return guest.References.at(kChest).Stacks == std::vector{Heirloom()}; }, "the guest's chest holds it as it is");
    }
    WHEN("a guest's player stores it")
    {
        party.As(party.Guest(), [&] { guest.PlayerSetsItems(kChest, kIronSword, 1, {Heirloom()}); });
        party.Until([&] { return host.References.at(kChest).Stacks == std::vector{Heirloom()}; }, "the host's chest holds it as it is");
    }
    CHECK(party.Differences().empty());
}
