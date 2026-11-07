#include <catch2/catch.hpp>

#include <Games/NpcSnapshot.h>

TEST_CASE("A resolved NPC captures its name separately from its chosen face", "[copies][npc-snapshot]")
{
    CHECK(NpcSnapshot::CaptureFlags(false, true) == NpcSnapshot::kFullName);
    CHECK(NpcSnapshot::CaptureFlags(false, false) == 0);
    CHECK(NpcSnapshot::CaptureFlags(true, false) == 0);
    CHECK(NpcSnapshot::CaptureFlags(true, true) == 0);
}

TEST_CASE("A temporary resolved NPC saves its name without a persistent change entry", "[copies][npc-snapshot]")
{
    CHECK(NpcSnapshot::SaveFlags(0, false, true) == NpcSnapshot::kFullName);
    CHECK(NpcSnapshot::SaveFlags(0x20A, false, true) == (0x20A | NpcSnapshot::kFullName));
    CHECK(NpcSnapshot::SaveFlags(0x20A, false, false) == 0x20A);
    CHECK(NpcSnapshot::SaveFlags(0x3000800, true, false) == 0x3000800);
}

TEST_CASE("A selected NPC sends no save data while a custom actor keeps its appearance", "[copies][npc-snapshot]")
{
    CHECK(NpcSnapshot::SaveFlags(0x20A, false, true, true) == 0);
    CHECK(NpcSnapshot::SaveFlags(0, false, true, true) == 0);
    CHECK(NpcSnapshot::SaveFlags(0x20A, false, true, false) == (0x20A | NpcSnapshot::kFullName));
    CHECK(NpcSnapshot::SaveFlags(0x3000800, true, true, true) == 0x3000800);
}

TEST_CASE("A copy keeps its game's resolved leveled base only when it has the owner's face", "[copies][npc-snapshot][copy-level]")
{
    CHECK(NpcSnapshot::KeepsResolvedBase(true, 0x3DEE0, 0x3DEE0));
    CHECK_FALSE(NpcSnapshot::KeepsResolvedBase(true, 0x3DEE1, 0x3DEE0));
    CHECK_FALSE(NpcSnapshot::KeepsResolvedBase(false, 0x3DEE0, 0x3DEE0));
    CHECK_FALSE(NpcSnapshot::KeepsResolvedBase(true, 0, 0));
}
