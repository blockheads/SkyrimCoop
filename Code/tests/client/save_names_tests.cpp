#include <Services/SaveNames.h>
#include <catch2/catch.hpp>

TEST_CASE("A save's character id is the one in its engine-made file name", "[save]")
{
    // Names from the 2026-11-08 playtest's and the user's Saves folders.
    CHECK(SaveNames::CharacterId("Save4_ABC467A6_0_7363726F6D626F6E697573_RiftenWorld_000123_20261108211500_1_1") == 0xABC467A6);
    CHECK(SaveNames::CharacterId("Save1_AADE737C_0_507269736F6E6572_Tamriel_000005_20251102180742_1_1.ess") == 0xAADE737C);
    CHECK(SaveNames::CharacterId("Save2_00000000_1_4368726973204372696E676C65_IvarsteadVilemyrInn_000014_20261031195031_1_1") == 0);
    CHECK(SaveNames::CharacterId("Quicksave0_92E593CB_1_746573746572_Tamriel_000008_20261031204500_1_1") == 0x92E593CB);
    CHECK(SaveNames::CharacterId("autosave3_92e593cb_1_746573746572_Tamriel_000008_20261031204500_1_1") == 0x92E593CB);
}

TEST_CASE("A save name without the engine's fields gives character 0", "[save]")
{
    // The party's scratch saves, console saves and names the engine's parse rejects.
    for (const char* cpName : {"SkyrimCoopWipe", "SkyrimCoopJoin", "MySave", "Save4_ABC467A6_0_73_Riften_000123_20261108211500_1",
                               "Save4_ABC467A6_0_73_Riften_000123_20261108211500_1_2", "Old4_ABC467A6_0_73_Riften_000123_20261108211500_1_1", ""})
        CHECK(SaveNames::CharacterId(cpName) == 0);
}
