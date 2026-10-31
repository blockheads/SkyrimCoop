// A guest's quest started by the host's state reads in its dialogue lines (Games/QuestStarts.h).
#include <catch2/catch.hpp>

#include <Games/QuestStarts.h>

TEST_CASE("A quest the host's state starts here reads its lines once, on the start", "[QuestStarts]")
{
    CHECK(QuestStarts::LoadsDialogue(0, 0x01));     // stopped -> running
    CHECK(QuestStarts::LoadsDialogue(0x02, 0x81));  // completed, stopped -> waiting to start
    CHECK_FALSE(QuestStarts::LoadsDialogue(0x01, 0x01)); // running already: read when it started
    CHECK_FALSE(QuestStarts::LoadsDialogue(0x80, 0x01));
    CHECK_FALSE(QuestStarts::LoadsDialogue(0x01, 0x00)); // stopping reads nothing
    CHECK_FALSE(QuestStarts::LoadsDialogue(0, 0x42));    // completed and failed, not started
}
