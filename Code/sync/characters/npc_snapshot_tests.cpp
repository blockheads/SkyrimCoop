#include <catch2/catch.hpp>

#include <characters/NpcSnapshot.h>

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

TEST_CASE("A leveled copy takes another pick as a new leveled base, never the record itself", "[copies][npc-snapshot][copy-level]")
{
    using NpcSnapshot::BaseApply;
    CHECK(NpcSnapshot::ChooseBaseApply(true, true, 0x23ABE, 0x23ABE) == BaseApply::Keep);
    CHECK(NpcSnapshot::ChooseBaseApply(true, true, 0x23AB7, 0x23ABE) == BaseApply::Relevel);
    CHECK(NpcSnapshot::ChooseBaseApply(true, false, 0, 0x23ABE) == BaseApply::Relevel); // a record from an older swap
    CHECK(NpcSnapshot::ChooseBaseApply(false, false, 0, 0x1C194) == BaseApply::Swap);
    CHECK(NpcSnapshot::ChooseBaseApply(false, true, 0x3DEE1, 0x3DEE0) == BaseApply::Swap);
}
