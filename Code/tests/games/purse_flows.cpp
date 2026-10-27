// The party's gold (docs/design/party-purse.md): one purse, the host's player's gold. Whoever's gold changes, and
// whoever changed it, every player (each game's own and its copies) carries the purse after.
#include "Party.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGold = 0x0000000F;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

std::vector<uint32_t> PlayerCopies(const FakeEngine& acEngine)
{
    std::vector<uint32_t> copies;
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player)
            copies.push_back(cId);
    return copies;
}

// Every game's player and every copy of a player there carries aGold.
bool AllCarry(Party& aParty, std::initializer_list<World*> aGames, int32_t aGold)
{
    for (World* pGame : aGames)
    {
        const FakeEngine& cEngine = Party::Engine(*pGame);
        if (cEngine.CountOf(FakeEngine::kPlayer, kGold) != aGold)
            return false;
        for (const uint32_t cCopy : PlayerCopies(cEngine))
            if (cEngine.CountOf(cCopy, kGold) != aGold)
                return false;
    }
    return true;
}

std::string Golds(std::initializer_list<World*> aGames)
{
    std::string text;
    for (World* pGame : aGames)
    {
        const FakeEngine& cEngine = Party::Engine(*pGame);
        text += std::format("[player {}", cEngine.CountOf(FakeEngine::kPlayer, kGold));
        for (const uint32_t cCopy : PlayerCopies(cEngine))
            text += std::format(", copy {}", cEngine.CountOf(cCopy, kGold));
        text += "] ";
    }
    return text;
}
} // namespace

SCENARIO("Every player carries the party's gold, whoever's changed", "[purse]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    const auto cGames = {&party.Host(), &party.Guest()};
    party.Until([&] { return PlayerCopies(host).size() == 1 && PlayerCopies(guest).size() == 1; },
                "each game has the other's player");
    const auto cCarry = [&](int32_t aGold, const std::string& acStep) {
        party.Until([&] { return AllCarry(party, cGames, aGold); }, acStep, [&] { return Golds(cGames); });
    };

    party.As(party.Guest(), [&] { guest.GetsItem(FakeEngine::kPlayer, kGold, 100); });
    cCarry(100, "the guest's loot is the party's: every player carries 100");

    WHEN("the host's player spends some")
    {
        party.As(party.Host(), [&] { host.LosesItem(FakeEngine::kPlayer, kGold, 40); });
        cCarry(60, "every player carries what is left");
        party.Agree("the games agree on the gold");
    }

    WHEN("the guest's player spends some")
    {
        party.As(party.Guest(), [&] { guest.LosesItem(FakeEngine::kPlayer, kGold, 30); });
        cCarry(70, "every player carries what is left");
    }

    WHEN("the host's script pays the guest's copy (a quest reward the guest's action ran, rule 2)")
    {
        party.As(party.Host(), [&] { host.GetsItem(PlayerCopies(host)[0], kGold, 25); });
        cCarry(125, "the reward is the party's");
    }

    WHEN("both spend at once, together more than the party has")
    {
        party.As(party.Host(), [&] { host.LosesItem(FakeEngine::kPlayer, kGold, 80); });
        party.As(party.Guest(), [&] { guest.LosesItem(FakeEngine::kPlayer, kGold, 70); });
        cCarry(0, "the purse is empty, not below");
    }
}

SCENARIO("A guest joining carries the party's gold, not what it brought", "[purse]")
{
    Party party(2, Plugins, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& first = Party::Engine(party.Guest(0));
    party.As(party.Host(), [&] { host.GetsItem(FakeEngine::kPlayer, kGold, 300); });
    party.As(party.Guest(0), [&] { first.GetsItem(FakeEngine::kPlayer, kGold, 999); });

    party.Join(party.Guest(0));
    const auto cTwo = {&party.Host(), &party.Guest(0)};
    party.Until([&] { return PlayerCopies(host).size() == 1 && AllCarry(party, cTwo, 300); },
                "the joining guest and its copy carry the party's 300", [&] { return Golds(cTwo); });

    party.As(party.Guest(0), [&] { first.GetsItem(FakeEngine::kPlayer, kGold, 50); });
    party.Until([&] { return AllCarry(party, cTwo, 350); }, "the purse is 350", [&] { return Golds(cTwo); });

    party.Join(party.Guest(1));
    const auto cAll = {&party.Host(), &party.Guest(0), &party.Guest(1)};
    party.Until([&] { return PlayerCopies(Party::Engine(party.Guest(1))).size() == 2 && AllCarry(party, cAll, 350); },
                "the second guest, its copies and the copies of it carry 350", [&] { return Golds(cAll); });
    party.Agree("every copy carries what its owner does");
}
