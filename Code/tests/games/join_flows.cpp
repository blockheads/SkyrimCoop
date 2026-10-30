// Characters in the host's save and joining with the host's world (docs/design/join-flow.md, "As built"): a guest's
// record reaches the host's store and its saves (HostRecords, CharacterRecords); a friend joining from a game of its
// own brings its character into the host's save, a friend the host's save knows joins from the main menu as that
// character (JoinService, HostSaveTransfer).
#include "Party.h"
#include "Fixtures.h"

#include <Events/MenuEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/NewGameEvent.h>
#include <Events/GameLoadedEvent.h>
#include <Messages/AuthenticationResponse.h>
#include <Events/SessionNoticeEvent.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Services/SessionService.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;

Inventory::Entry Item(uint32_t aBase, int32_t aCount, bool aQuest = false)
{
    Inventory::Entry entry;
    entry.BaseId = GameId(0, aBase);
    entry.Count = aCount;
    entry.IsQuestItem = aQuest;
    return entry;
}

// A character of the guest's own, other than the host's player in every part the record has.
PlayerRecord LydiasCharacter()
{
    PlayerRecord record;
    record.Name = "Lydia";
    record.Level = 12;
    record.Base = std::string("lydia's base\0face", 17);
    record.BaseFlags = 0x03000E36;
    record.FaceTints.Entries.push_back({"SkinTone", 0.5f, 0xFF8080, 1});
    record.Weight = 87.5f;
    record.LevelExperience = 40.f;
    record.Skills[0] = {45.f, 3.f, 80.f, 0};
    record.PerkPoints = 1;
    record.Perks = {{GameId(0, 0xBABE4), 1}};
    record.Spells = {GameId(0, 0x12FCD)};
    record.Values = {{24, 180.f}};
    record.PermanentValues = {{24, 195.f}, {32, 350.f}};
    record.Items.Entries = {Item(0xF, 300)};
    return record;
}

PlayerRecord HostsCharacter()
{
    PlayerRecord record;
    record.Name = "Dovahkiin";
    record.Level = 30;
    record.Base = "the host's base";
    record.Perks = {{GameId(0, 0x79342), 3}};
    return record;
}

// The game's player is that character: its record's parts, its items in its inventory (as the engine keeps them).
void Plays(World& aGame, PlayerRecord aRecord)
{
    FakeEngine& engine = Party::Engine(aGame);
    engine.Actors.at(FakeEngine::kPlayer).Items = aRecord.Items;
    aRecord.Items = {};
    engine.Player = std::move(aRecord);
}

CharacterRecords& Records(World& aGame)
{
    return aGame.ctx().at<SaveService>().GetRecords();
}

JoinService& Join(World& aGame)
{
    return aGame.ctx().at<JoinService>();
}

std::string KeyOf(World& aGame)
{
    return Party::Engine(aGame).PlayerId();
}

// The character as the record compares, without its place and key, its souls and words (party_share_flows.cpp).
PlayerRecord Character(PlayerRecord aRecord)
{
    aRecord.Key.clear();
    aRecord.Cell = {};
    aRecord.WorldSpace = {};
    aRecord.Position = {};
    std::erase_if(aRecord.Values, [](const PlayerRecord::Value& acValue) { return acValue.Index == 133; });
    aRecord.Words.clear();
    return aRecord;
}

// The record with the party's gold in place of its own (party-purse.md): aGold of Gold001, none at 0.
PlayerRecord WithGold(PlayerRecord aRecord, int32_t aGold)
{
    std::erase_if(aRecord.Items.Entries, [](const Inventory::Entry& acEntry) { return acEntry.BaseId == GameId(0, 0xF); });
    if (aGold)
        aRecord.Items.Entries.push_back(Item(0xF, aGold));
    return aRecord;
}

std::string Describe(const JoinStatus& acStatus)
{
    return std::format("{} {} {}", ToString(acStatus.Current), acStatus.Reason, acStatus.Message);
}

std::string HostEndpoint(World& aHost)
{
    return std::format("127.0.0.1:{}", aHost.GetHostService().GetNetwork()->GetPort());
}

// The friend's game leaves the session and goes back to the main menu: no save loaded, its player the game's default.
void ToMainMenu(Party& aParty, World& aGuest)
{
    if (aGuest.GetTransport().IsConnected())
    {
        aParty.Leave(aGuest);
        aParty.Until([&] { return !aGuest.GetTransport().IsConnected(); }, "the friend's game left the session");
    }
    FakeEngine& engine = Party::Engine(aGuest);
    engine.AtMainMenu = true;
    Plays(aGuest, {});
}

// The friend's game joins with the host's world, from wherever it is, until it plays in the session.
void JoinsWithWorld(Party& aParty, World& aHost, World& aGuest, const char* acpStep)
{
    bool started = false;
    aParty.As(aGuest, [&] { started = Join(aGuest).Join(HostEndpoint(aHost)); });
    REQUIRE(started);
    aParty.Until([&] { return aGuest.GetTransport().IsOnline() && !Join(aGuest).IsJoining(); }, acpStep,
                  [&] { return Describe(Join(aGuest).GetStatus()); });
}
} // namespace

SCENARIO("A guest brings and restores its own learned knowledge instead of the host's", "[records][knowledge]")
{
    Party party(1, SmokeWorld, false);
    World& host = party.Host();
    World& guest = party.Guest();
    Plays(host, HostsCharacter());
    Plays(guest, LydiasCharacter());
    auto& hostEngine = Party::Engine(host);
    auto& guestEngine = Party::Engine(guest);
    hostEngine.Knowledge = {.Present = true, .Ingredients = {{GameId(0, 0x77E1C), 8, 7}},
                           .Enchantments = {GameId(0, 0x10FB95)}};
    PlayerKnowledge own{.Present = true, .Ingredients = {{GameId(0, 0x77E1C), 1, 1}},
                        .Enchantments = {GameId(0, 0x10FB96)}};
    SECTION("a character with its own learned effects") {}
    SECTION("a new character that has learned nothing") { own = {.Present = true}; }
    guestEngine.Knowledge = own;
    JoinsWithWorld(party, host, guest, "the guest brings its knowledge into the host's world");
    CHECK(guestEngine.Knowledge == own);
    const auto cKey = KeyOf(guest);
    party.Until([&] { return Records(host).Find(cKey) && Records(host).Find(cKey)->Knowledge == own; },
                "the host keeps the guest's knowledge");
    own.Ingredients = {{GameId(0, 0x77E1C), 5, 2}};
    guestEngine.Knowledge = own;
    party.As(guest, [&] { guest.GetDispatcher().trigger(MenuEvent{"InventoryMenu", false}); });
    party.Until([&] { return Records(host).Find(cKey)->Knowledge == own; }, "the host receives newly learned knowledge");
    ToMainMenu(party, guest);
    guestEngine.Knowledge = {};
    JoinsWithWorld(party, host, guest, "a fresh guest restores its saved knowledge");
    CHECK(guestEngine.Knowledge == own);
    CHECK(hostEngine.Knowledge.Enchantments == std::vector<GameId>{GameId(0, 0x10FB95)});
}

SCENARIO("A character joining again keeps its brewed potions, made again from the record's recipes", "[records][potions]")
{
    Party party(1, SmokeWorld, false);
    World& host = party.Host();
    World& guest = party.Guest();
    Plays(host, HostsCharacter());
    Plays(guest, LydiasCharacter());
    auto& guestEngine = Party::Engine(guest);
    JoinsWithWorld(party, host, guest, "the guest joins with the host's world");
    const Recipe cFortify{false, {{25.f, 0, 60, 30.f, GameId(0, 0x3EAF3)}}};
    party.As(guest, [&] {
        guestEngine.PlayerBrews(cFortify, 2);
        guest.GetDispatcher().trigger(MenuEvent{"InventoryMenu", false});
    });
    const auto cKey = KeyOf(guest);
    party.Until([&] { const auto* cpRecord = Records(host).Find(cKey); return cpRecord && cpRecord->Recipes == std::vector{cFortify}; },
                "the host keeps the guest's record with its potion's recipe");

    // Back at the main menu, its game's created forms are gone (a save's load replaces them).
    ToMainMenu(party, guest);
    guestEngine.Brewed.clear();
    JoinsWithWorld(party, host, guest, "the guest joins again from the main menu");
    uint32_t made = 0;
    party.As(guest, [&] { made = ItemForm(cFortify.Id()); });
    CHECK(made != 0);
    CHECK(guestEngine.Brewed.at(made) == cFortify);
    int32_t carried = 0;
    for (const auto& cEntry : guestEngine.Actors.at(FakeEngine::kPlayer).Items.Entries)
        if (cEntry.BaseId == cFortify.Id())
            carried += cEntry.Count;
    CHECK(carried == 2);
}

SCENARIO("A guest's character reaches the host's save, and loading that save brings it back", "[records]")
{
    Party party(1, SmokeWorld, false);
    World& host = party.Host();
    World& guest = party.Guest();
    Plays(host, HostsCharacter());
    Plays(guest, LydiasCharacter());
    party.Join(guest);
    const std::string cKey = KeyOf(guest);

    party.Until([&] { return Records(host).Find(cKey) != nullptr; }, "the host's game keeps the guest's character from its joining");
    THEN("it is the guest's character, under its key, and the host's own player has no record")
    {
        CHECK(Character(*Records(host).Find(cKey)) == Character(LydiasCharacter()));
        CHECK(Records(host).All().size() == 1);
    }

    WHEN("the guest's character changes, then the host saves")
    {
        Party::Engine(guest).Player.Level = 13;
        party.As(guest, [&] { guest.GetDispatcher().trigger(MenuEvent{"StatsMenu", false}); });
        party.Until([&] { return Records(host).Find(cKey)->Level == 13; }, "the host's game has the guest's new level");

        // The guest walks off; the host's copy of it follows.
        auto& guestPlayer = Party::Engine(guest).Actors.at(FakeEngine::kPlayer);
        guestPlayer.Position[0] += 500.f;
        party.Until(
            [&] {
                for (const uint32_t cCopy : Party::Engine(host).CopiesOf())
                    if (std::abs(Party::Engine(host).Actors.at(cCopy).Position[0] - guestPlayer.Position[0]) < 1.f)
                        return true;
                return false;
            },
            "the host's copy of the guest walks with it");

        std::optional<IJoinBridge::SaveFiles> save;
        party.As(host, [&] { (void)Party::Engine(host).SaveNow("Save1"); });
        party.Frame();
        party.As(host, [&] { save = Party::Engine(host).SaveNow("Save1"); });
        REQUIRE(save);
        THEN("the save's co-save holds the guest's character at the place of the host's copy of it")
        {
            CharacterRecords saved;
            REQUIRE(saved.Decode(save->CoSave, CharacterRecords::kVersion));
            REQUIRE(saved.Find(cKey));
            CHECK(saved.Find(cKey)->Level == 13);
            CHECK(std::abs(saved.Find(cKey)->Position[0] - guestPlayer.Position[0]) < 1.f);
        }

        AND_WHEN("the host's game loads it again later")
        {
            party.As(host, [&] {
                Records(host).Clear();
                host.ctx().at<SaveService>().ReadCoSave(save->CoSave, CharacterRecords::kVersion);
            });
            THEN("the guest's character is back") CHECK(Records(host).Find(cKey)->Level == 13);
        }
    }

    WHEN("the guest picks up gold (our AddInventoryItem hook raises it)")
    {
        party.As(guest, [&] { Party::Engine(guest).GetsItem(FakeEngine::kPlayer, 0xF, 123); });
        const auto gold = [&](const PlayerRecord& acRecord) {
            int32_t count = 0;
            for (const auto& cEntry : acRecord.Items.Entries)
                if (cEntry.BaseId == GameId(0, 0xF))
                    count += cEntry.Count;
            return count;
        };
        // The party's gold: the host's none, the guest's 300 left behind at its joining, and the 123.
        party.Until([&] { return gold(*Records(host).Find(cKey)) == 123; }, "the host's record of the guest has the party's gold");
    }

    WHEN("a game sends a record under another player's key")
    {
        PlayerRecordRequest request;
        request.Record = LydiasCharacter();
        request.Record.Key = "steam:76561198000000099";
        party.As(guest, [&] { guest.GetTransport().Send(request); });
        party.Frame();
        party.Frame();
        THEN("the host's game keeps nothing under that key") CHECK(Records(host).Find("steam:76561198000000099") == nullptr);
    }
}

SCENARIO("A friend brings its character from a game of its own into the host's world, then joins from the main menu as it", "[join]")
{
    Party party(1, SmokeWorld, false);
    World& host = party.Host();
    World& guest = party.Guest();
    Plays(host, HostsCharacter());
    PlayerRecord own = LydiasCharacter();
    own.Items.Entries.push_back(Item(0x999A, 1, true)); // a quest item of its own world
    Plays(guest, own);
    const std::string cKey = KeyOf(guest);

    bool refusedSave = false;
    party.As(guest, [&] { REQUIRE(Join(guest).Join(HostEndpoint(host))); });
    party.Until([&] { return Join(guest).GetStatus().Current == JoinStatus::State::kReceivingWorld; },
                "the host's gate lets the friend's game in; the host's save is on its way",
                [&] { return Describe(Join(guest).GetStatus()); });
    party.As(guest, [&] { refusedSave = !Party::Engine(guest).PlayerSaves("Quicksave0"); });
    THEN("the friend's game cannot save while it joins") CHECK(refusedSave);

    party.Until([&] { return guest.GetTransport().IsOnline() && !Join(guest).IsJoining(); }, "the friend's game loads the host's save and joins",
                [&] { return Describe(Join(guest).GetStatus()); });
    party.Until([&] { return Party::Engine(guest).CountOf(FakeEngine::kPlayer, 0xF) == 0; }, "its gold is the party's: the host's none");
    THEN("its game loaded the host's save once, and its player is the character it brought, where the save put it, "
         "without its own world's quest items, carrying the party's gold")
    {
        CHECK(Party::Engine(guest).Loaded == std::vector<std::string>{JoinService::kJoinSave});
        CHECK(Party::Engine(host).Saves == std::vector<std::string>{JoinService::kTransferSave});
        CHECK(Character(Party::Engine(guest).ReadPlayer()) == Character(WithGold(LydiasCharacter(), 0)));
        CHECK(Party::Engine(guest).Actors.at(FakeEngine::kPlayer).Cell == Party::Engine(host).Actors.at(FakeEngine::kPlayer).Cell);
        CHECK(Join(guest).GetStatus().Current == JoinStatus::State::kJoined);
    }
    party.Agree("each game has the other's player");
    party.Until([&] { return Records(host).Find(cKey) != nullptr; }, "the host's game keeps the character it brought");

    WHEN("its character levels up while it plays, then it leaves and joins again from the main menu")
    {
        Party::Engine(guest).Player.Level = 13;
        party.As(guest, [&] { guest.GetDispatcher().trigger(MenuEvent{"LevelUp Menu", false}); });
        party.Until([&] { return Records(host).Find(cKey)->Level == 13; }, "the host's game has the friend's new level");
        ToMainMenu(party, guest);
        JoinsWithWorld(party, host, guest, "the friend's game joins from the main menu");
        THEN("it plays the character the host's save keeps, at its new level, with the party's gold")
        {
            PlayerRecord expected = WithGold(LydiasCharacter(), 0);
            expected.Level = 13;
            CHECK(Character(Party::Engine(guest).ReadPlayer()) == Character(expected));
        }
    }

    WHEN("it joins again from a game of its own with another character")
    {
        party.Leave(guest);
        party.Until([&] { return !guest.GetTransport().IsConnected(); }, "the friend's game left the session");
        PlayerRecord other = HostsCharacter();
        other.Name = "Uthgerd";
        Plays(guest, other);
        JoinsWithWorld(party, host, guest, "the friend's game joins from another game of its own");
        THEN("the host's save's character wins over the one it brought")
        CHECK(Character(Party::Engine(guest).ReadPlayer()) == Character(LydiasCharacter()));
    }
}

SCENARIO("A character joining again keeps its own souls and the words it unlocked; the party's words come with the host's save",
         "[join][dragonborn]")
{
    constexpr uint32_t kFus = 0x13E22;
    constexpr uint32_t kRo = 0x13E23;
    Party party(1, SmokeWorld, false);
    World& host = party.Host();
    World& guest = party.Guest();
    FakeEngine& hostEngine = Party::Engine(host);
    FakeEngine& guestEngine = Party::Engine(guest);
    Plays(host, HostsCharacter());
    Plays(guest, LydiasCharacter());
    JoinsWithWorld(party, host, guest, "the friend's game joins with the host's world");
    const std::string cKey = KeyOf(guest);

    // A word from the host's script, a dragon's soul for both; the friend spends its own on the word.
    party.As(host, [&] { hostEngine.TeachesWord(kFus); });
    party.As(host, [&] { hostEngine.ModifiesSouls(FakeEngine::kPlayer, 1.f, FakeEngine::kPlayer); });
    party.Until([&] { return guestEngine.WordsKnown.contains(kFus) && guestEngine.Souls[FakeEngine::kPlayer] == 1.f; },
                "the friend's player learns the word and gains the soul");
    party.As(guest, [&] {
        guestEngine.SpendsSoul(kFus);
        guest.GetDispatcher().trigger(MenuEvent{"MagicMenu", false});
    });
    party.Until([&] { const auto* cpRecord = Records(host).Find(cKey); return cpRecord && !cpRecord->Words.empty(); },
                "the host's game keeps the friend's unlocked word");

    WHEN("the host's script teaches another word while the friend is away, and the friend joins again from the main menu")
    {
        ToMainMenu(party, guest);
        party.As(host, [&] { hostEngine.TeachesWord(kRo); });
        JoinsWithWorld(party, host, guest, "the friend's game joins again from the main menu");
        THEN("the friend's player knows both words, has Fus unlocked and no soul; the host's player keeps its own")
        {
            CHECK(guestEngine.WordsKnown.contains(kFus));
            CHECK(guestEngine.WordsKnown.contains(kRo));
            CHECK(guestEngine.WordsUnlocked.contains(kFus));
            CHECK(guestEngine.Souls[FakeEngine::kPlayer] == 0.f);
            CHECK(hostEngine.Souls[FakeEngine::kPlayer] == 1.f);
            CHECK(!hostEngine.WordsUnlocked.contains(kFus));
            CHECK(guestEngine.SoulsAbsorbedShown == 1); // the dragon's, not the join's
        }
    }

    WHEN("the host spends a soul while the friend is away")
    {
        ToMainMenu(party, guest);
        party.As(host, [&] {
            hostEngine.TeachesWord(kRo);
            hostEngine.SpendsSoul(kRo);
        });
        REQUIRE(hostEngine.WordsUnlocked.contains(kRo));
        JoinsWithWorld(party, host, guest, "the friend joins without inheriting the host's purchased unlock");
        CHECK(guestEngine.WordsKnown.contains(kRo));
        CHECK(guestEngine.WordsUnlocked.contains(kFus));
        CHECK_FALSE(guestEngine.WordsUnlocked.contains(kRo));
        CHECK(hostEngine.WordsUnlocked.contains(kRo));
        CHECK(guestEngine.Souls[FakeEngine::kPlayer] == 0.f);
    }
}

SCENARIO("A returning character gets story spells granted and removed while it was away", "[join][dragonborn][story-spells]")
{
    constexpr uint32_t kBeastForm = 0x92C48;
    Party party(1, SmokeWorld, false);
    World& host = party.Host();
    World& guest = party.Guest();
    auto& hostEngine = Party::Engine(host);
    auto& guestEngine = Party::Engine(guest);
    Plays(host, HostsCharacter());
    Plays(guest, LydiasCharacter());
    JoinsWithWorld(party, host, guest, "the friend joins with its own character");
    party.Until([&] { return Records(host).Find(KeyOf(guest)) != nullptr; }, "the host keeps the friend's character");
    ToMainMenu(party, guest);
    party.As(host, [&] { hostEngine.AddsSpell(FakeEngine::kPlayer, kBeastForm, true); });
    party.Frame();
    JoinsWithWorld(party, host, guest, "the friend returns after the story gave Beast Form");
    CHECK(guestEngine.AddedSpells[FakeEngine::kPlayer].contains(kBeastForm));
    CHECK(guestEngine.ReadPlayer().Spells == std::vector<GameId>{GameId(0, 0x12FCD), GameId(0, kBeastForm)});
    ToMainMenu(party, guest);
    party.As(host, [&] { hostEngine.AddsSpell(FakeEngine::kPlayer, kBeastForm, false); });
    party.Frame();
    JoinsWithWorld(party, host, guest, "the friend returns after the story removed Beast Form");
    CHECK_FALSE(guestEngine.AddedSpells[FakeEngine::kPlayer].contains(kBeastForm));
    CHECK(guestEngine.ReadPlayer().Spells == LydiasCharacter().Spells);
}

TEST_CASE("Story spell state survives the co-save and older character-only saves still load", "[records][story-spells]")
{
    CharacterRecords records;
    records.SetStorySpell(GameId(0, 0x92C48), true);
    records.SetStorySpell(GameId(0, 0x12FCD), false);
    CharacterRecords loaded;
    REQUIRE(loaded.Decode(records.Encode(), CharacterRecords::kVersion));
    auto character = LydiasCharacter();
    loaded.ApplyStorySpells(character);
    CHECK(character.Spells == std::vector<GameId>{GameId(0, 0x92C48)});
    CHECK(character.Skills == LydiasCharacter().Skills);
    CHECK(character.Perks == LydiasCharacter().Perks);
    REQUIRE(loaded.Decode(std::string(1, '\0'), CharacterRecords::kVersion));
    character = LydiasCharacter();
    loaded.ApplyStorySpells(character);
    CHECK(character == LydiasCharacter());
    const std::string truncated("\0\1", 2); // no records, one spell, no spell bytes
    CHECK_FALSE(loaded.Decode(truncated, CharacterRecords::kVersion));
    character = LydiasCharacter();
    loaded.ApplyStorySpells(character);
    CHECK(character == LydiasCharacter());
    const auto cEncoded = records.Encode();
    for (size_t size = 2; size < cEncoded.size(); ++size)
    {
        INFO("truncated to " << size << " of " << cEncoded.size() << " bytes");
        CHECK_FALSE(loaded.Decode(std::string_view(cEncoded).substr(0, size), CharacterRecords::kVersion));
        character = LydiasCharacter();
        loaded.ApplyStorySpells(character);
        CHECK(character == LydiasCharacter());
    }
    CHECK_FALSE(loaded.Decode(std::string("\0\x80", 2), CharacterRecords::kVersion));
}

SCENARIO("A join that cannot finish says why", "[join]")
{
    Party party(1, SmokeWorld, false);
    World& host = party.Host();
    World& guest = party.Guest();

    WHEN("nothing listens at the address")
    {
        party.As(guest, [&] { Join(guest).Join("127.0.0.1:9"); });
        party.Until([&] { return Join(guest).GetStatus().Current == JoinStatus::State::kRefused; }, "the friend's game gives up");
        CHECK(Join(guest).GetStatus().Reason == "unreachable");
        CHECK(!Join(guest).IsJoining());
    }

    WHEN("the bundled creation data is unavailable to an unknown friend at the main menu")
    {
        ToMainMenu(party, guest);
        Party::Engine(guest).CreationAvailable = false;
        party.As(guest, [&] { Join(guest).Join(HostEndpoint(host)); });
        party.Until([&] { return Join(guest).GetStatus().Current == JoinStatus::State::kRefused; }, "the friend's game is refused",
                    [&] { return Describe(Join(guest).GetStatus()); });
        CHECK(Join(guest).GetStatus().Reason == "creation_unavailable");
        CHECK(Party::Engine(guest).Loaded.empty());
        CHECK(Party::Engine(host).Saves.empty());
    }

    WHEN("the host stops hosting while its save is on its way")
    {
        Party::Engine(host).RefusesSaves = true; // the transfer waits for the host's game to save
        party.As(guest, [&] { Join(guest).Join(HostEndpoint(host)); });
        party.Until([&] { return Join(guest).GetStatus().Current == JoinStatus::State::kReceivingWorld; }, "the gate passes");
        party.As(host, [&] { host.GetHostService().StopHosting(); });
        party.Until([&] { return Join(guest).GetStatus().Current == JoinStatus::State::kRefused; }, "the friend's game gives up",
                    [&] { return Describe(Join(guest).GetStatus()); });
        CHECK(Join(guest).GetStatus().Reason == "lost_connection");
        THEN("its game can save again") CHECK(guest.ctx().at<SaveService>().MaySave());
    }
}

SCENARIO("A brought character lands beside the host after checked placement", "[join][join-placement]")
{
    Party party(1, SmokeWorld, false);
    auto& host = party.Host();
    auto& guest = party.Guest();
    auto& engine = Party::Engine(guest);
    Plays(host, HostsCharacter());
    Plays(guest, LydiasCharacter());
    SECTION("there is room immediately")
    {
        JoinsWithWorld(party, host, guest, "the guest brings its character into the host's world");
    }
    SECTION("joining waits until checked placement finds room")
    {
        engine.PlacementClear = false;
        party.As(guest, [&] { REQUIRE(Join(guest).Join(HostEndpoint(host))); });
        party.Until([&] { return engine.PlacementChecks > 0; }, "the loaded world's first checked placement");
        CHECK_FALSE(guest.GetTransport().IsOnline());
        CHECK(Join(guest).GetStatus().Current == JoinStatus::State::kCreatingCharacter);
        engine.PlacementClear = true;
        party.Until([&] { return guest.GetTransport().IsOnline() && !Join(guest).IsJoining(); },
                    "joining finishes after a clear spot is found");
    }
    const auto& cHost = Party::Engine(host).Actors.at(FakeEngine::kPlayer);
    const auto& cGuest = engine.Actors.at(FakeEngine::kPlayer);
    CHECK(std::hypot(cHost.Position[0] - cGuest.Position[0], cHost.Position[1] - cGuest.Position[1]) >= 100.f);
    CHECK(engine.PlacementChecks > 0);
}

SCENARIO("A guest that leaves and joins again arrives where it left, never on the host's spot", "[join][join-placement]")
{
    Party party(1, SmokeWorld, false);
    auto& host = party.Host();
    auto& guest = party.Guest();
    Plays(host, HostsCharacter());
    Plays(guest, LydiasCharacter());
    JoinsWithWorld(party, host, guest, "the guest brings its character into the host's world");
    const auto cKey = KeyOf(guest);
    party.Until([&] { return Records(host).Find(cKey) != nullptr; }, "the host's game keeps the guest's character");
    const auto& cHost = Party::Engine(host).Actors.at(FakeEngine::kPlayer);
    const auto cApart = [&] {
        const auto& cGuest = Party::Engine(guest).Actors.at(FakeEngine::kPlayer);
        return std::hypot(cHost.Position[0] - cGuest.Position[0], cHost.Position[1] - cGuest.Position[1]);
    };

    WHEN("it walks off, quits to the main menu and joins again (the host saves for it after it left)")
    {
        auto& walker = Party::Engine(guest).Actors.at(FakeEngine::kPlayer);
        walker.Position[0] += 500.f;
        const float cLeftAt = walker.Position[0];
        party.Until(
            [&] {
                for (const uint32_t cCopy : Party::Engine(host).CopiesOf())
                    if (std::abs(Party::Engine(host).Actors.at(cCopy).Position[0] - cLeftAt) < 1.f)
                        return true;
                return false;
            },
            "the host's copy of the guest walks with it");
        // Its record changing after the save it was placed at must not lose its place (a record carries none).
        Party::Engine(guest).Player.Level = 14;
        party.As(guest, [&] { guest.GetDispatcher().trigger(MenuEvent{"StatsMenu", false}); });
        party.Until([&] { return Records(host).Find(cKey)->Level == 14; }, "the host has the guest's new level");
        ToMainMenu(party, guest);
        party.Until([&] { return host.GetSessionService().GetPlayers().empty(); }, "the host's game drops the guest that left");
        CHECK(Records(host).Find(cKey)->Position[0] == Approx(cLeftAt));
        JoinsWithWorld(party, host, guest, "the guest joins again as its kept character");
        THEN("it stands where it left")
        CHECK(std::abs(Party::Engine(guest).Actors.at(FakeEngine::kPlayer).Position[0] - cLeftAt) < 1.f);
    }

    WHEN("the host's save keeps its character without a place")
    {
        ToMainMenu(party, guest);
        PlayerRecord placeless = *Records(host).Find(cKey);
        party.As(host, [&] {
            Records(host).Remove(cKey);
            placeless.Cell = {};
            placeless.WorldSpace = {};
            placeless.Position = {};
            Records(host).Put(placeless);
        });
        JoinsWithWorld(party, host, guest, "the guest joins again as its kept character");
        THEN("checked placement puts it beside the host, not on the host's spot") CHECK(cApart() >= 100.f);
    }
}

SCENARIO("An unknown friend creates a character as part of its original join", "[join][creation]")
{
    Party party(1, SmokeWorld, false);
    auto& host = party.Host();
    auto& guest = party.Guest();
    auto& engine = Party::Engine(guest);
    ToMainMenu(party, guest);
    const auto endpoint = HostEndpoint(host);
    party.As(guest, [&] { REQUIRE(Join(guest).Join(endpoint)); });
    party.Until([&] { return Join(guest).IsCreatingCharacter(); }, "the host asks the unknown friend to create a character");
    CHECK(Join(guest).GetStatus().Current == JoinStatus::State::kCreatingCharacter);
    CHECK(engine.CreationRequests == 1);
    CHECK(engine.Loaded.empty());
    CHECK(Party::Engine(host).Saves.empty());
    CHECK_FALSE(guest.GetHostService().IsHosting());
    party.As(guest, [&] {
        CHECK_FALSE(Join(guest).Join(endpoint));
        AuthenticationResponse duplicate;
        duplicate.Type = AuthenticationResponse::ResponseType::kNoCharacter;
        guest.GetDispatcher().trigger(duplicate);
        guest.GetDispatcher().trigger(NewGameEvent{});
        guest.GetDispatcher().trigger(MenuEvent{"Loading Menu", false});
    });
    CHECK(engine.CreationRequests == 1);
    CHECK_FALSE(guest.GetHostService().IsHosting());
    engine.AtMainMenu = false;
    engine.CreationPhase = IJoinBridge::Creation::kCustomizing;
    party.As(guest, [&] { guest.GetDispatcher().trigger(UpdateEvent{JoinService::kLoadSeconds * 4}); });
    REQUIRE(Join(guest).IsJoining());
    REQUIRE(engine.Loaded.empty());

    SECTION("naming and setup finish, then its character joins and is kept for the next invite")
    {
        auto made = LydiasCharacter();
        made.Name = "Rinara";
        made.Knowledge = {.Present = true};
        Plays(guest, made);
        engine.Knowledge = {.Present = true};
        engine.CreationPhase = IJoinBridge::Creation::kFinalizing;
        party.Frame();
        CHECK(engine.Loaded.empty());
        CHECK(Party::Engine(host).Saves.empty());
        engine.CreationPhase = IJoinBridge::Creation::kComplete;
        party.Until([&] { return guest.GetTransport().IsOnline() && !Join(guest).IsJoining(); }, "creation automatically resumes the original join");
        party.Until([&] { return engine.CountOf(FakeEngine::kPlayer, 0xF) == 0; }, "the host supplies the party purse");
        CHECK(Character(engine.ReadPlayer()) == Character(WithGold(made, 0)));
        CHECK(engine.Knowledge.Present);
        party.Until([&] { return Records(host).Find(KeyOf(guest)) != nullptr; }, "the host keeps the new character");
        ToMainMenu(party, guest);
        JoinsWithWorld(party, host, guest, "the next invite uses the kept character");
        CHECK(engine.Player.Name == "Rinara");
        CHECK(engine.CreationRequests == 1);
    }
    SECTION("cancelling leaves the creator usable and never automatically joins")
    {
        party.As(guest, [&] { Join(guest).Cancel(""); });
        Plays(guest, LydiasCharacter());
        engine.CreationPhase = IJoinBridge::Creation::kComplete;
        party.Frame();
        CHECK_FALSE(Join(guest).IsJoining());
        CHECK(engine.Player.Name == "Lydia");
        CHECK(engine.Loaded.empty());
        CHECK(Party::Engine(host).Saves.empty());
    }
    SECTION("losing the host leaves the creator usable and clears automatic joining")
    {
        party.As(guest, [&] { Join(guest).CreationHostLost(); });
        CHECK(Join(guest).GetStatus().Reason == "lost_connection");
        Plays(guest, LydiasCharacter());
        engine.CreationPhase = IJoinBridge::Creation::kComplete;
        party.Frame();
        CHECK_FALSE(Join(guest).IsJoining());
        CHECK(engine.Loaded.empty());
    }
    SECTION("finalization has the loading deadline")
    {
        engine.CreationPhase = IJoinBridge::Creation::kFinalizing;
        party.As(guest, [&] { guest.GetDispatcher().trigger(UpdateEvent{JoinService::kLoadSeconds + 1}); });
        CHECK(Join(guest).GetStatus().Reason == "creation_failed");
        CHECK_FALSE(Join(guest).IsJoining());
    }
    SECTION("a failed final load keeps the created character available for retry")
    {
        Plays(guest, LydiasCharacter());
        engine.CreationPhase = IJoinBridge::Creation::kComplete;
        party.As(guest, [&] { guest.GetDispatcher().trigger(UpdateEvent{0.01}); });
        party.Until([&] { return Join(guest).GetStatus().Current == JoinStatus::State::kReceivingWorld; }, "the finished character passes the host's gate");
        party.As(host, [&] { host.GetHostService().StopHosting(); });
        party.Until([&] { return !Join(guest).IsJoining(); }, "the interrupted transfer fails");
        CHECK(engine.Player.Name == "Lydia");
        CHECK(engine.CreationRequests == 1);
        party.As(host, [&] { REQUIRE(host.GetHostService().StartHosting(static_cast<uint16_t>(std::stoi(endpoint.substr(endpoint.find(':') + 1))))); });
        JoinsWithWorld(party, host, guest, "the created character can retry joining");
        CHECK(engine.Player.Name == "Lydia");
        CHECK(engine.CreationRequests == 1);
    }
}

SCENARIO("Character startup has a deadline before the customization menu opens", "[join][creation]")
{
    Party party(1, SmokeWorld, false);
    auto& guest = party.Guest();
    ToMainMenu(party, guest);
    party.As(guest, [&] { REQUIRE(Join(guest).Join(HostEndpoint(party.Host()))); });
    party.Until([&] { return Join(guest).IsCreatingCharacter(); }, "the bundled start is requested");
    party.As(guest, [&] { guest.GetDispatcher().trigger(UpdateEvent{JoinService::kLoadSeconds + 1}); });
    CHECK(Join(guest).GetStatus().Reason == "creation_failed");
    CHECK(Party::Engine(guest).Loaded.empty());
}
