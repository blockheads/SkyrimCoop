// Party items (docs/design/party-items.md): keys, quest items and the story's rewards are the party's. Every player
// carries a copy named "[P] <name>"; dropping one takes it from everyone, picking it up gives it back to everyone.
// Ordinary loot stays the finder's own.
#include "Party.h"
#include "Fixtures.h"

#include <Games/Overrides.h>

namespace
{
constexpr uint32_t kSword = 0x00012EB7; // Iron Sword
constexpr const char* kTagged = "[P] Iron Sword";

// Every game's engine names the sword, as the game's data does.
void NameSword(Party& aParty)
{
    for (World* pGame : {&aParty.Host(), &aParty.Guest()})
        Party::Engine(*pGame).FormNames[kSword] = "Iron Sword";
}

// The party's copies the actor carries: its stacks of the sword named with the tag.
int32_t PartyCopies(const FakeEngine& acEngine, uint32_t aActor)
{
    int32_t count = 0;
    for (const auto& cEntry : acEngine.EntriesOf(aActor, kSword))
        if (cEntry.ExtraName == kTagged)
            count += cEntry.Count;
    return count;
}

// Every game's player and every player copy carries aCount of the party's swords.
bool AllCarry(std::initializer_list<World*> aGames, int32_t aCount)
{
    for (World* pGame : aGames)
    {
        const FakeEngine& cEngine = Party::Engine(*pGame);
        if (PartyCopies(cEngine, FakeEngine::kPlayer) != aCount)
            return false;
        for (const uint32_t cCopy : PlayerCopies(cEngine))
            if (PartyCopies(cEngine, cCopy) != aCount)
                return false;
    }
    return true;
}

std::string Swords(std::initializer_list<World*> aGames)
{
    std::string text;
    for (World* pGame : aGames)
    {
        const FakeEngine& cEngine = Party::Engine(*pGame);
        text += std::format("[player {} of {}", PartyCopies(cEngine, FakeEngine::kPlayer), cEngine.CountOf(FakeEngine::kPlayer, kSword));
        for (const uint32_t cCopy : PlayerCopies(cEngine))
            text += std::format(", copy {} of {}", PartyCopies(cEngine, cCopy), cEngine.CountOf(cCopy, kSword));
        text += "] ";
    }
    return text;
}
} // namespace

SCENARIO("A quest's reward is the party's: every player carries it, tagged", "[party-items]")
{
    Party party(1, SmokeWorld);
    NameSword(party);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    const auto cGames = {&party.Host(), &party.Guest()};
    party.Until([&] { return PlayerCopies(host).size() == 1 && PlayerCopies(guest).size() == 1; },
                "each game has the other's player");
    const auto cCarry = [&](int32_t aCount, const std::string& acStep) {
        party.Until([&] { return AllCarry(cGames, aCount); }, acStep, [&] { return Swords(cGames); });
    };

    party.As(party.Host(), [&] { host.QuestGivesItem(FakeEngine::kPlayer, kSword, 1); });
    cCarry(1, "every player carries the reward as \"[P] Iron Sword\"");

    WHEN("the guest drops it")
    {
        party.As(party.Guest(), [&] { guest.Drops(FakeEngine::kPlayer, kSword, 1); });
        cCarry(0, "no player carries it any more");
    }
}

SCENARIO("A quest's reward on a guest's character is the party's too", "[party-items]")
{
    Party party(1, SmokeWorld);
    NameSword(party);
    FakeEngine& host = Party::Engine(party.Host());
    const auto cGames = {&party.Host(), &party.Guest()};
    party.Until([&] { return PlayerCopies(host).size() == 1; }, "the host has the guest's player");
    party.As(party.Host(), [&] { host.QuestGivesItem(PlayerCopies(host)[0], kSword, 1); }); // rule 2: the guest's copy
    party.Until([&] { return AllCarry(cGames, 1); }, "every player carries it", [&] { return Swords(cGames); });
}

SCENARIO("Ordinary loot stays the finder's own", "[party-items]")
{
    Party party(1, SmokeWorld);
    NameSword(party);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopies(host).size() == 1; }, "the host has the guest's player");
    party.As(party.Guest(), [&] { guest.GetsItem(FakeEngine::kPlayer, kSword, 1); });
    party.Until([&] { return host.CountOf(PlayerCopies(host)[0], kSword) == 1; }, "the host's copy of the guest carries it");
    THEN("the host's player does not, and nobody's is tagged")
    {
        CHECK(host.CountOf(FakeEngine::kPlayer, kSword) == 0);
        CHECK(PartyCopies(guest, FakeEngine::kPlayer) == 0);
    }
}

SCENARIO("A guest joining carries the party's items, not stale ones it brought", "[party-items]")
{
    constexpr uint32_t kKey = 0x00093B08; // Key to Breezehome
    Party party(1, SmokeWorld, false);
    NameSword(party);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.QuestGivesItem(FakeEngine::kPlayer, kSword, 1); });
    party.Until([&] { return PartyCopies(host, FakeEngine::kPlayer) == 1; }, "the host's player carries the party's sword");
    // The guest's record carries a party key from an earlier session that the party no longer has.
    Inventory::Entry stale;
    stale.BaseId = GameId{0, kKey};
    stale.Count = 1;
    stale.ExtraName = "[P] Key to Breezehome";
    party.As(party.Guest(), [&] { ScopedInventoryOverride _; guest.GetsItem(FakeEngine::kPlayer, stale); });

    party.Join(party.Guest());
    const auto cGames = {&party.Host(), &party.Guest()};
    party.Until([&] { return PlayerCopies(host).size() == 1 && AllCarry(cGames, 1); },
                "the joining guest and its copy carry the party's sword", [&] { return Swords(cGames); });
    party.Until([&] { return guest.CountOf(FakeEngine::kPlayer, kKey) == 0 && host.CountOf(PlayerCopies(host)[0], kKey) == 0; },
                "the stale party key is gone from the guest and its copy");
}

SCENARIO("A guest picks up a quest object: the party holds the quest item, every player carries it", "[party-items]")
{
    static constexpr uint32_t kClaw = 0x00039647; // the Golden Claw, MS13's GoldenClaw alias holding it
    static constexpr uint32_t kCell = 0x00031D2E;
    Party party(1, [](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
        aEngine.FormNames[kClaw] = "Golden Claw";
    });
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopies(host).size() == 1 && PlayerCopies(guest).size() == 1; }, "each game has the other's player");
    Inventory::Entry claw;
    claw.BaseId = GameId(0, kClaw);
    claw.Count = 1;
    party.As(party.Host(), [&] {
        const uint32_t cId = 0xFF000000 | host.CreateLoose(claw, kCell)->BaseId;
        host.Loose.at(cId).QuestObject = true;
    });
    party.Until([&] { return guest.Loose.size() == 1; }, "the claw lies in the guest's world");
    party.As(party.Guest(), [&] { guest.PicksUp(guest.Loose.begin()->first); });

    const auto cClaws = [&] {
        return std::format("host player {} (quest {}), host's copy {}, guest player {}, guest's copy {}", host.CountOf(FakeEngine::kPlayer, kClaw),
                           std::ranges::count_if(host.EntriesOf(FakeEngine::kPlayer, kClaw), [](const auto& e) { return e.IsQuestItem; }),
                           host.CountOf(PlayerCopies(host)[0], kClaw), guest.CountOf(FakeEngine::kPlayer, kClaw), guest.CountOf(PlayerCopies(guest)[0], kClaw));
    };
    const auto cOneEach = [&] {
        return host.CountOf(FakeEngine::kPlayer, kClaw) == 1 && host.CountOf(PlayerCopies(host)[0], kClaw) == 1 &&
               guest.CountOf(FakeEngine::kPlayer, kClaw) == 1 && guest.CountOf(PlayerCopies(guest)[0], kClaw) == 1;
    };
    party.Until(cOneEach, "every player and copy carries one claw", cClaws);
    THEN("the host's player holds the quest item, and it stays one each")
    {
        CHECK(std::ranges::count_if(host.EntriesOf(FakeEngine::kPlayer, kClaw), [](const auto& e) { return e.IsQuestItem; }) == 1);
        const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        party.Until([&] { return std::chrono::steady_clock::now() > cUntil; }, "a while passes");
        CHECK(cOneEach());
        INFO(cClaws());
    }
}

SCENARIO("Tempering or enchanting a party item improves every player's copy", "[party-items]")
{
    Party party(1, SmokeWorld);
    NameSword(party);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    const auto cGames = {&party.Host(), &party.Guest()};
    party.Until([&] { return PlayerCopies(host).size() == 1 && PlayerCopies(guest).size() == 1; }, "each game has the other's player");
    party.As(party.Host(), [&] { host.QuestGivesItem(FakeEngine::kPlayer, kSword, 1); });
    party.Until([&] { return AllCarry(cGames, 1); }, "every player carries the party's sword", [&] { return Swords(cGames); });

    // Every player's and copy's party sword, as (health, enchantment).
    const auto cImproved = [&](float aHealth, uint32_t aEnchantment) {
        for (World* pGame : cGames)
        {
            const FakeEngine& cEngine = Party::Engine(*pGame);
            std::vector<uint32_t> holders = PlayerCopies(cEngine);
            holders.push_back(FakeEngine::kPlayer);
            for (const uint32_t cHolder : holders)
            {
                const auto cStacks = cEngine.EntriesOf(cHolder, kSword);
                if (cStacks.size() != 1 || cStacks[0].ExtraName != kTagged || cStacks[0].ExtraHealth != aHealth ||
                    cStacks[0].ExtraEnchantId.BaseId != aEnchantment)
                    return false;
            }
        }
        return true;
    };
    const auto cDescribe = [&] {
        std::string text;
        for (World* pGame : cGames)
            for (const auto& cStack : Party::Engine(*pGame).EntriesOf(FakeEngine::kPlayer, kSword))
                text += std::format("[{} x{} health {} ench {:X}] ", cStack.ExtraName, cStack.Count, cStack.ExtraHealth, cStack.ExtraEnchantId.BaseId);
        return text;
    };
    Inventory::Entry sword = guest.EntriesOf(FakeEngine::kPlayer, kSword).at(0);
    party.As(party.Guest(), [&] { guest.Improves(FakeEngine::kPlayer, sword, [](Inventory::Entry& aSword) { aSword.ExtraHealth = 1.2f; }); });
    party.Until([&] { return cImproved(1.2f, 0); }, "the guest tempered it: every copy is tempered", cDescribe);

    sword = host.EntriesOf(FakeEngine::kPlayer, kSword).at(0);
    party.As(party.Host(), [&] {
        host.Improves(FakeEngine::kPlayer, sword, [](Inventory::Entry& aSword) {
            aSword.ExtraEnchantId = GameId(0, 0x4605A);
            aSword.ExtraEnchantCharge = 1000;
        });
    });
    party.Until([&] { return cImproved(1.2f, 0x4605A); }, "the host enchanted it: every copy is enchanted", cDescribe);
}

SCENARIO("A quest's gold reward goes to the party's purse, not a party item", "[party-items][purse]")
{
    // quest-gold-turn-in-host (Skyrim 2026-10-31): Lucan's 400 gold, given by the quest's script, was taken back as
    // "[P] Gold", a new party item, and reached no one's purse.
    constexpr uint32_t kGold = 0x0000000F;
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopies(host).size() == 1 && PlayerCopies(guest).size() == 1; },
                "each game has the other's player");
    const int32_t cBefore = host.CountOf(FakeEngine::kPlayer, kGold);
    const auto cTaggedGold = [](const FakeEngine& acEngine) {
        return std::ranges::any_of(acEngine.EntriesOf(FakeEngine::kPlayer, kGold),
                                   [](const auto& acEntry) { return acEntry.ExtraName.starts_with("[P] "); });
    };

    party.As(party.Host(), [&] { host.QuestGivesItem(FakeEngine::kPlayer, kGold, 400); });
    party.Until([&] { return host.CountOf(FakeEngine::kPlayer, kGold) == cBefore + 400 &&
                             guest.CountOf(FakeEngine::kPlayer, kGold) == cBefore + 400; },
                "every player carries the reward as the party's gold",
                [&] { return std::format("host {}, guest {}", host.CountOf(FakeEngine::kPlayer, kGold), guest.CountOf(FakeEngine::kPlayer, kGold)); });
    CHECK_FALSE(cTaggedGold(host));
    CHECK_FALSE(cTaggedGold(guest));
}
