#include <core/CharacterRecords.h>
#include <Wire/Serialization.h>
#include <catch2/catch.hpp>

TEST_CASE("A host co-save keeps each guest's ingredient and enchantment knowledge", "[records][knowledge]")
{
    CharacterRecords records;
    PlayerRecord guest;
    guest.Key = "guest";
    guest.Knowledge.Present = true;
    guest.Knowledge.Ingredients = {{GameId(0, 0x77E1C), 5, 2}, {GameId(2, 0xABCD), 15, 3}};
    guest.Knowledge.Enchantments = {GameId(0, 0x10FB95), GameId(2, 0xABC)};
    records.Put(guest);
    CharacterRecords loaded;
    REQUIRE(loaded.Decode(records.Encode(), CharacterRecords::kVersion));
    REQUIRE(loaded.Find("guest"));
    CHECK(loaded.Find("guest")->Knowledge == guest.Knowledge);
}

TEST_CASE("A host co-save keeps each guest's own favorites, effects, cooldowns and view beside its character", "[records][own-state]")
{
    CharacterRecords records;
    PlayerRecord guest;
    guest.Key = "guest";
    guest.Own.Present = true;
    Inventory::Entry sword;
    sword.BaseId = GameId(0, 0x12EB7);
    sword.Count = 1;
    sword.ExtraHealth = 1.1f;
    guest.Own.FavoriteItems = {{sword, 2}, {Inventory::Entry{.BaseId = GameId(0, 0x3EADE), .Count = 4}, -1}};
    guest.Own.Globals = {{PlayerOwnState::kMagicFavorites, std::string("\x04\x00\xCD\x7F\x00", 5)}, {PlayerOwnState::kPlayerControls, std::string(6, '\x01')}};
    guest.Own.Damage = {-40.f, -12.5f, 0.f};
    guest.Own.Effects = {{GameId(0, 0xFB99A), 0, 120.f, 1440.f}, {GameId(0, 0x5AD5C), 3, 2.5f, 60.f}};
    guest.Own.Cooldowns = {{GameId(0, 0xE40C8), 1300.f}};
    guest.Own.Trainings = 3;
    guest.Own.FirstPerson = true;
    records.Put(guest);
    CharacterRecords loaded;
    REQUIRE(loaded.Decode(records.Encode(), CharacterRecords::kVersion));
    REQUIRE(loaded.Find("guest"));
    CHECK(loaded.Find("guest")->Own == guest.Own);
}

TEST_CASE("A record an older build saved has no own state, so a load applies none of it", "[records][own-state]")
{
    PlayerRecord original;
    original.Name = "Older character";
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    original.Serialize(writer);
    // The previous layout ended before the own state's 'present' flag.
    Wire::Buffer::Reader reader(std::span(buffer.GetData(), writer.Size() - 1));
    PlayerRecord read;
    read.Own.Present = true;
    read.Own.Trainings = 4;
    read.Deserialize(reader);
    CHECK(read.Own == PlayerOwnState{});
    CHECK(read.Name == original.Name);
}

TEST_CASE("A joined guest's timed effects and cooldowns age until a save, and stop when it leaves", "[records][own-state]")
{
    using namespace std::chrono_literals;
    CharacterRecords records;
    PlayerRecord guest;
    guest.Key = "guest";
    guest.Own.Present = true;
    guest.Own.Effects = {{GameId(0, 0xFB99A), 0, 100.f, 1440.f}, {GameId(0, 0xFB99A), 1, 100.f, 130.f}};
    guest.Own.Cooldowns = {{GameId(0, 0xE40C8), 50.f}, {GameId(0, 0xE40C9), 500.f}};
    const auto cReceived = CharacterRecords::Clock::time_point{} + 1000s;
    records.Put(guest, cReceived);
    const auto cRead = [&](CharacterRecords::Clock::time_point aNow) {
        CharacterRecords loaded;
        REQUIRE(loaded.Decode(records.Encode(aNow), CharacterRecords::kVersion));
        return loaded.Find("guest")->Own;
    };
    // A save 60 s after the record came: the short effect and cooldown ran out, the others ran on.
    const auto cSaved = cRead(cReceived + 60s);
    REQUIRE(cSaved.Effects.size() == 1);
    CHECK(cSaved.Effects[0].Elapsed == Approx(160.f));
    REQUIRE(cSaved.Cooldowns.size() == 1);
    CHECK(cSaved.Cooldowns[0].Seconds == Approx(440.f));
    // Left 100 s after it came: kept as it left, however much later the host saves.
    records.Settle("guest", cReceived + 100s);
    const auto cAway = cRead(cReceived + 5000s);
    REQUIRE(cAway.Effects.size() == 1);
    CHECK(cAway.Effects[0].Elapsed == Approx(200.f));
    CHECK(cAway.Cooldowns[0].Seconds == Approx(400.f));
}

TEST_CASE("Only the player's globals in a layout their engine loader reads within its bytes are kept", "[records][own-state]")
{
    // MagicFavorites: a count (36004: 1 << 2) and a form id (two varints), then an empty hotkey list.
    const std::string cFavorites("\x04\x00\xCD\x7F\x00", 5);
    CHECK(PlayerOwnState::GlobalReadable(PlayerOwnState::kMagicFavorites, cFavorites));
    {
        // As MagicFavorites::SaveGame writes it under our save hook (SaveLoad.cpp, WriteId): Flames, no keys.
        Wire::Buffer buffer;
        Wire::Buffer::Writer writer(&buffer);
        const uint8_t cOne = 1 << 2, cNone = 0;
        writer.WriteBytes(std::span(&cOne, 1));
        Wire::Serialization::WriteVarInt(writer, 0);
        Wire::Serialization::WriteVarInt(writer, 0x12FCD);
        writer.WriteBytes(std::span(&cNone, 1));
        CHECK(PlayerOwnState::GlobalReadable(PlayerOwnState::kMagicFavorites,
                                             std::string(reinterpret_cast<const char*>(buffer.GetData()), writer.Size())));
    }
    CHECK_FALSE(PlayerOwnState::GlobalReadable(PlayerOwnState::kMagicFavorites, cFavorites.substr(0, 3)));
    CHECK_FALSE(PlayerOwnState::GlobalReadable(PlayerOwnState::kMagicFavorites, std::string("\xFC\xFF", 2))); // a count of 63
    CHECK_FALSE(PlayerOwnState::GlobalReadable(PlayerOwnState::kMagicFavorites, std::string("\x03", 1)));     // kind 3
    // Misc stats: a u32 count, a u16-length name, a category byte, a u32 value. A name past 1023 overflows 36002's stack.
    const std::string stat = std::string("\x01\x00\x00\x00\x04\x00", 6) + "Days" + std::string("\x00\x05\x00\x00\x00", 5);
    CHECK(PlayerOwnState::GlobalReadable(PlayerOwnState::kMiscStats, stat));
    std::string longName("\x01\x00\x00\x00\x00\x04", 6);
    longName += std::string(1024 + 5, 'n');
    CHECK_FALSE(PlayerOwnState::GlobalReadable(PlayerOwnState::kMiscStats, longName));
    CHECK(PlayerOwnState::GlobalReadable(PlayerOwnState::kPlayerControls, std::string(6, '\0')));
    CHECK_FALSE(PlayerOwnState::GlobalReadable(102, std::string(6, '\0')));

    // A record holding another type, or a global cut short, keeps none of them.
    PlayerOwnState state{.Present = true};
    state.Globals = {{102, std::string(6, '\0')}, {PlayerOwnState::kMagicFavorites, cFavorites}};
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    state.Serialize(writer);
    Wire::Buffer::Reader reader(std::span(buffer.GetData(), writer.Size()));
    PlayerOwnState read;
    read.Deserialize(reader);
    CHECK(read.Globals == std::map<uint32_t, std::string>{{PlayerOwnState::kMagicFavorites, cFavorites}});
    Wire::Buffer::Reader cut(std::span(buffer.GetData(), 8));
    read.Deserialize(cut);
    CHECK(read == PlayerOwnState{});
}

TEST_CASE("An older character record has no knowledge snapshot", "[records][knowledge]")
{
    PlayerRecord original;
    original.Name = "Older character";
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    original.Serialize(writer);
    REQUIRE(writer.Size() > 1);
    // The previous layout ended before the final 'snapshot present' varint.
    Wire::Buffer::Reader reader(std::span(buffer.GetData(), writer.Size() - 1));
    PlayerRecord read;
    read.Knowledge.Present = true;
    read.Deserialize(reader);
    CHECK_FALSE(read.Knowledge.Present);
    CHECK(read.Name == original.Name);
}

TEST_CASE("Truncated character counts and record lengths cannot load a co-save", "[records][truncated-records]")
{
    for (const auto& cData : {std::string("\x80", 1), std::string("\1\x80", 2)})
    {
        CharacterRecords records;
        PlayerRecord record;
        record.Key = "existing";
        records.Put(record);
        REQUIRE(records.All().size() == 1);
        CHECK_FALSE(records.Decode(cData, CharacterRecords::kVersion));
        CHECK(records.All().empty());
    }
}

TEST_CASE("A guest's new record keeps the place the host's game set", "[records][place]")
{
    CharacterRecords records;
    PlayerRecord guest;
    guest.Key = "guest";
    records.Put(guest);
    records.SetPlace("guest", GameId(0, 0x3C), GameId{}, {100.f, 200.f, 300.f});
    guest.Level = 12;
    records.Put(guest);
    REQUIRE(records.Find("guest"));
    CHECK(records.Find("guest")->Level == 12);
    CHECK(records.Find("guest")->HasPlace());
    CHECK(records.Find("guest")->Position == std::array<float, 3>{100.f, 200.f, 300.f});
}

TEST_CASE("A version 3 co-save's guests load with their items, which carried no owner then", "[records]")
{
    // Written before Inventory::Entry::ExtraOwner: guest "Older guest" with a sword named "Old Friend" and 3 sweetrolls.
    const std::string cV3(
    "\x01\xE5\x02\x05\x67\x75\x65\x73\x74\x0B\x4F\x6C\x64\x65\x72\x20\x67\x75\x65\x73\x74\x01\x00\x00"
    "\x00\x00\x00\x48\x42\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\xB7\xDD\x04\x00\x01\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x40\x01\x4F\x6C\x64\x20\x46\x72\x69\x65\x6E\x64\xBD\x96\x19"
    "\x00\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x01\x95\xF7\x43\x00\x00"
    "\x00",
        361);
    CharacterRecords records;
    REQUIRE(records.Decode(cV3, 3));
    const PlayerRecord* pGuest = records.Find("guest");
    REQUIRE(pGuest);
    CHECK(pGuest->Name == "Older guest");
    REQUIRE(pGuest->Items.Entries.size() == 2);
    CHECK(pGuest->Items.Entries[0].BaseId == GameId(0, 0x12EB7));
    CHECK(pGuest->Items.Entries[0].ExtraName == "Old Friend");
    CHECK(pGuest->Items.Entries[0].ExtraOwner == GameId{});
    CHECK(pGuest->Items.Entries[1].BaseId == GameId(0, 0x64B3D));
    CHECK(pGuest->Items.Entries[1].Count == 3);
    CHECK(pGuest->Knowledge.Enchantments == std::vector{GameId(0, 0x10FB95)});
}
