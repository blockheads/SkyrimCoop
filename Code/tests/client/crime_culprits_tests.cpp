#include <catch2/catch.hpp>

#include <Services/CrimeCulprits.h>

TEST_CASE("A crime keeps the guest who did it until the record is reused", "[crime]")
{
    CrimeCulprits culprits;
    int crime{}, other{};

    culprits.Remember(&crime, 0xFF000834);
    REQUIRE(culprits.Of(&crime) == 0xFF000834);
    REQUIRE(culprits.Of(&other) == 0);

    SECTION("a host crime at the same address has no culprit")
    {
        culprits.Remember(&crime, 0);
        REQUIRE(culprits.Of(&crime) == 0);
    }
    SECTION("leaving the session forgets them all")
    {
        culprits.Clear();
        REQUIRE(culprits.Of(&crime) == 0);
    }
}
