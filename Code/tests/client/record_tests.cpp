#include <Services/CharacterRecords.h>
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
    guest.Own.Globals = {{PlayerOwnState::kMagicFavorites, std::string("\x01\x00\x12\xFC", 4)}, {PlayerOwnState::kMiscStats, std::string(300, 's')}};
    guest.Own.Damage = {-40.f, -12.5f, 0.f};
    guest.Own.Effects = {{GameId(0, 0xFB99A), 0, 120.f}, {GameId(0, 0x5AD5C), 3, 2.5f}};
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
