#include <catch2/catch.hpp>

#include <Games/NpcSnapshot.h>

TEST_CASE("A resolved NPC captures its name separately from its chosen face", "[copies][npc-snapshot]")
{
    CHECK(NpcSnapshot::CaptureFlags(false, true) == NpcSnapshot::kFullName);
    CHECK(NpcSnapshot::CaptureFlags(false, false) == 0);
    CHECK(NpcSnapshot::CaptureFlags(true, false) == 0);
    CHECK(NpcSnapshot::CaptureFlags(true, true) == 0);
}

TEST_CASE("Ordinary runtime NPC attributes do not request a resolved-name overlay", "[copies][npc-snapshot]")
{
    CHECK_FALSE(NpcSnapshot::HasResolvedName(0x20A));
    CHECK_FALSE(NpcSnapshot::HasResolvedName(0));
    CHECK(NpcSnapshot::HasResolvedName(NpcSnapshot::kFullName));
    CHECK(NpcSnapshot::HasResolvedName(0x20A | NpcSnapshot::kFullName));
}

TEST_CASE("A temporary resolved NPC saves its name without a persistent change entry", "[copies][npc-snapshot]")
{
    CHECK(NpcSnapshot::SaveFlags(0, false, true) == NpcSnapshot::kFullName);
    CHECK(NpcSnapshot::SaveFlags(0x20A, false, true) == (0x20A | NpcSnapshot::kFullName));
    CHECK(NpcSnapshot::SaveFlags(0x20A, false, false) == 0x20A);
    CHECK(NpcSnapshot::SaveFlags(0x3000800, true, false) == 0x3000800);
}

TEST_CASE("A selected NPC sends only the name section while a custom actor keeps its appearance", "[copies][npc-snapshot]")
{
    CHECK(NpcSnapshot::SaveFlags(0x20A, false, true, true) == NpcSnapshot::kFullName);
    CHECK(NpcSnapshot::SaveFlags(0, false, true, true) == NpcSnapshot::kFullName);
    CHECK(NpcSnapshot::SaveFlags(0x20A, false, true, false) == (0x20A | NpcSnapshot::kFullName));
    CHECK(NpcSnapshot::SaveFlags(0x3000800, true, true, true) == 0x3000800);
}
