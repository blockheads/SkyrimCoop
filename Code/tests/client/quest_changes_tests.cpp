#include <Games/QuestChanges.h>
#include <catch2/catch.hpp>

TEST_CASE("Quest writes coalesce until the frame reads them", "[Quests]")
{
    QuestChanges changes;
    changes.Quest(10);
    changes.Quest(10);
    changes.Quest(20);
    CHECK(changes.Take() == std::set<uint32_t>{10, 20});
    CHECK(changes.Take().empty());
}

TEST_CASE("An alias reference's cell or sharing change invalidates its quests", "[Quests]")
{
    QuestChanges changes;
    changes.Track(10, {100, 200});
    changes.Track(20, {200});
    changes.Reference(300);
    CHECK(changes.Take().empty());
    changes.Reference(200);
    CHECK(changes.Take() == std::set<uint32_t>{10, 20});

    changes.Track(10, {300});
    changes.Reference(200);
    CHECK(changes.Take() == std::set<uint32_t>{20});
    changes.Track(20, {});
    changes.Reference(200);
    CHECK(changes.Take().empty());
    changes.Reference(300);
    CHECK(changes.Take() == std::set<uint32_t>{10});

    changes.Clear();
    changes.Reference(300);
    CHECK(changes.Take().empty());
}
