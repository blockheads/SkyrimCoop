#include <BSCore/LockOrderHistory.h>
#include <catch2/catch.hpp>

#include <array>
#include <unordered_map>

TEST_CASE("lock history keeps writer evidence separate from the first reader", "[locks]")
{
    // Same lock addresses observed during save reads, then later alias writes.
    constexpr uint64_t cReference = 0x1000, cAlias = 0x2000;
    const auto cReaders = LockOrderHistory::Key(cReference, cAlias, 0, 0);
    const auto cWriters = LockOrderHistory::Key(cReference, cAlias, 1, 1);
    std::unordered_map<uint64_t, std::array<uint64_t, 3>> origins;
    origins.try_emplace(cReaders, std::array<uint64_t, 3>{384, 0x16F48B, 0x182F95});
    origins.try_emplace(cWriters, std::array<uint64_t, 3>{444, 0x16D3BB, 0x1820EF});
    REQUIRE(origins.size() == 2);
    CHECK(origins.at(cWriters)[0] == 444);
    CHECK(origins.at(cWriters)[1] == 0x16D3BB);
    CHECK(origins.at(cReaders)[0] == 384);
    for (uint8_t outer = 0; outer < 2; ++outer)
        for (uint8_t inner = 0; inner < 2; ++inner)
            CHECK(LockOrderHistory::Key(cReference, cAlias, outer, inner) !=
                  LockOrderHistory::Key(cAlias, cReference, inner, outer));
    CHECK(LockOrderHistory::Key(cReference, cAlias, 0, 1) != cReaders);
    CHECK(LockOrderHistory::Key(cReference, cAlias, 1, 0) != cWriters);
    CHECK(LockOrderHistory::Key(cReference, cAlias, 0, 1) != LockOrderHistory::Key(cReference, cAlias, 1, 0));
}

TEST_CASE("a pair seen on an object destroyed since is not the pair of the object now at its address", "[locks]")
{
    // An extra data list freed and allocated again at the same address (bugs.md, "A granted pickup's lock order").
    constexpr uint64_t cList = 0x27BC0F88, cOther = 0x2812B498;
    CHECK(LockOrderHistory::Key(cList, cOther, 0, 1) == LockOrderHistory::Key(cList, cOther, 0, 1, 0, 0));
    CHECK(LockOrderHistory::Key(cList, cOther, 0, 1, 1, 0) != LockOrderHistory::Key(cList, cOther, 0, 1, 0, 0));
    CHECK(LockOrderHistory::Key(cList, cOther, 0, 1, 0, 1) != LockOrderHistory::Key(cList, cOther, 0, 1, 0, 0));
    CHECK(LockOrderHistory::Key(cList, cOther, 0, 1, 1, 0) != LockOrderHistory::Key(cList, cOther, 0, 1, 0, 1));
}

TEST_CASE("a destroyed lock's generation is its own: another lock sharing its slot keeps its pairs", "[locks]")
{
    // The review of PR #118: live A shares a slot with destroyed C; A -> B, C destroyed, then B -> A must still match.
    LockOrderHistory::LockGenerations<16> generations;
    const uint64_t cA = 0x1000, cC = cA + (uint64_t{16} << 3); // the same home slot
    REQUIRE(generations.Home(cA) == generations.Home(cC));
    const auto cRecorded = LockOrderHistory::Key(cA, 0x2000, 0, 1, generations.Of(cA), generations.Of(0x2000));
    generations.Destroyed(cC);
    CHECK(generations.Of(cC) == 1);
    CHECK(generations.Of(cA) == 0);
    CHECK(LockOrderHistory::Key(cA, 0x2000, 0, 1, generations.Of(cA), generations.Of(0x2000)) == cRecorded);
    generations.Destroyed(cA);
    generations.Destroyed(cA);
    CHECK(generations.Of(cA) == 2);
    CHECK(generations.Of(cC) == 1);
}

TEST_CASE("an inverted ask is live only while another thread holds the lock from the reverse order", "[locks]")
{
    // mounted-guest-leaves: the main thread holds a reference's extra data (17A479) and asks for the havok world lock
    // (E82682); a worker took the world lock at E85432 (returning to E85437) before reading extra data.
    struct Entry
    {
        const void* Lock;
        uint64_t Caller;
        bool Acquired;
    };
    int world = 0, other = 0;
    constexpr uint64_t cStep = 0xE85437, cElsewhere = 0x2F2920;
    using LockOrderHistory::Overlap;
    using LockOrderHistory::OverlapOf;
    CHECK(OverlapOf(std::array<Entry, 1>{{{&other, cStep, true}}}, &world, cStep) == Overlap::kNone);
    CHECK(OverlapOf(std::array<Entry, 1>{{{&world, cStep, false}}}, &world, cStep) == Overlap::kNone); // still waiting
    CHECK(OverlapOf(std::array<Entry, 1>{{{&world, cElsewhere, true}}}, &world, cStep) == Overlap::kContended);
    CHECK(OverlapOf(std::array<Entry, 2>{{{&world, cElsewhere, true}, {&world, cStep, true}}}, &world, cStep) == Overlap::kLive);
}
