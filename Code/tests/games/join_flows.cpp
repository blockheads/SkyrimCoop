// Characters in the host's save and joining with the host's world (docs/design/join-flow.md, "As built"): a guest's
// record reaches the host's store and its saves (HostRecords, CharacterRecords); a friend joining from a game of its
// own brings its character into the host's save, a friend the host's save knows joins from the main menu as that
// character (JoinService, HostSaveTransfer).
#include "Party.h"

#include <Events/MenuEvent.h>
#include <Events/SessionNoticeEvent.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

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

// The character as the record compares, without its place and key.
PlayerRecord Character(PlayerRecord aRecord)
{
    aRecord.Key.clear();
    aRecord.Cell = {};
    aRecord.WorldSpace = {};
    aRecord.Position = {};
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

SCENARIO("A guest's character reaches the host's save, and loading that save brings it back", "[records]")
{
    Party party(1, Plugins, false);
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
        party.Until([&] { return gold(*Records(host).Find(cKey)) == 300 + 123; }, "the host's record of the guest has the gold");
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
    Party party(1, Plugins, false);
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
    THEN("its game loaded the host's save once, and its player is the character it brought, where the save put it, "
         "without its own world's quest items")
    {
        CHECK(Party::Engine(guest).Loaded == std::vector<std::string>{JoinService::kJoinSave});
        CHECK(Party::Engine(host).Saves == std::vector<std::string>{JoinService::kTransferSave});
        CHECK(Character(Party::Engine(guest).ReadPlayer()) == Character(LydiasCharacter()));
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
        THEN("it plays the character the host's save keeps, at its new level")
        {
            PlayerRecord expected = LydiasCharacter();
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

SCENARIO("A join that cannot finish says why", "[join]")
{
    Party party(1, Plugins, false);
    World& host = party.Host();
    World& guest = party.Guest();

    WHEN("nothing listens at the address")
    {
        party.As(guest, [&] { Join(guest).Join("127.0.0.1:9"); });
        party.Until([&] { return Join(guest).GetStatus().Current == JoinStatus::State::kRefused; }, "the friend's game gives up");
        CHECK(Join(guest).GetStatus().Reason == "unreachable");
        CHECK(!Join(guest).IsJoining());
    }

    WHEN("a friend the host's save does not know joins from the main menu")
    {
        ToMainMenu(party, guest);
        party.As(guest, [&] { Join(guest).Join(HostEndpoint(host)); });
        party.Until([&] { return Join(guest).GetStatus().Current == JoinStatus::State::kRefused; }, "the friend's game is refused",
                    [&] { return Describe(Join(guest).GetStatus()); });
        CHECK(Join(guest).GetStatus().Reason == "no_character");
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
