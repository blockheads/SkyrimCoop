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
