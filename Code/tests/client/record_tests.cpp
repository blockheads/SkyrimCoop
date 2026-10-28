#include <Services/CharacterRecords.h>
#include <catch2/catch.hpp>

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
