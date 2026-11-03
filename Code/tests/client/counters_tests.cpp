#include <catch2/catch.hpp>

#include <Diagnostics/Counters.h>

#include <algorithm>

namespace
{
Diagnostics::Counters::Reading ReadingOf(const char* acpName)
{
    const auto cAll = Diagnostics::Counters::Read();
    const auto it = std::ranges::find(cAll, std::string(acpName), &Diagnostics::Counters::Reading::Name);
    REQUIRE(it != cAll.end());
    return *it;
}
} // namespace

TEST_CASE("A counter keeps the count, sum and max of what it was given, until reset", "[counters]")
{
    for (const int cValue : {3, 9, 4})
        COUNT("test values", cValue);
    auto reading = ReadingOf("test values");
    CHECK(reading.Count == 3);
    CHECK(reading.Sum == 16);
    CHECK(reading.Max == 9);
    Diagnostics::Counters::Reset();
    reading = ReadingOf("test values");
    CHECK(reading.Count == 0);
    CHECK(reading.Max == 0);
}

TEST_CASE("A timed scope counts once per pass", "[counters]")
{
    for (int i = 0; i < 2; ++i)
    {
        COUNT_TIME("test timed scope");
    }
    CHECK(ReadingOf("test timed scope").Count == 2);
}
