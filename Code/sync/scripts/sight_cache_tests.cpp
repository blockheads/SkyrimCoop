// The host's copy of each guest's sight (docs/re/systems/papyrus-guest-scripts.md, "A player's line of sight").
#include <catch2/catch.hpp>

#include <scripts/SightCache.h>

namespace
{
constexpr uint32_t kGuest = 0xFF000835;
constexpr uint32_t kMarker = 0x709D4;
const SightCache::Clock::time_point kStart{};
} // namespace

SCENARIO("The host reads a guest's sight from the guest's game", "[sight]")
{
    SightCache sight;
    const auto cFirst = sight.Sees(kGuest, kMarker, kStart);

    THEN("until its game answers it is no, and one question goes out")
    {
        CHECK_FALSE(cFirst.Sees);
        REQUIRE(cFirst.Ask);
        CHECK(cFirst.Ask->Guest == kGuest);
        CHECK(cFirst.Ask->Target == kMarker);
        CHECK(SightCache::IsSightReply(cFirst.Ask->ReplyId));
        CHECK_FALSE(sight.Sees(kGuest, kMarker, kStart + std::chrono::milliseconds(20)).Ask);
    }
    WHEN("its game answers yes")
    {
        REQUIRE(sight.Answer(cFirst.Ask->ReplyId, true, kStart + std::chrono::milliseconds(50)));
        const auto cThen = sight.Sees(kGuest, kMarker, kStart + std::chrono::milliseconds(60));
        THEN("it sees, and the next question goes out") CHECK((cThen.Sees && cThen.Ask));
        THEN("an answer older than a second is no")
            CHECK_FALSE(sight.Sees(kGuest, kMarker, kStart + std::chrono::milliseconds(1100)).Sees);
    }
    WHEN("its game never answers (it left)")
        THEN("the question goes out again after two seconds")
            CHECK(sight.Sees(kGuest, kMarker, kStart + std::chrono::milliseconds(2100)).Ask);
    WHEN("an answer comes for a question asked again since")
    {
        const auto cAgain = sight.Sees(kGuest, kMarker, kStart + std::chrono::milliseconds(2100));
        REQUIRE(sight.Answer(cFirst.Ask->ReplyId, true, kStart + std::chrono::milliseconds(2150)));
        THEN("only the newer question's answer counts")
            CHECK_FALSE(sight.Sees(kGuest, kMarker, kStart + std::chrono::milliseconds(2160)).Sees);
        REQUIRE(sight.Answer(cAgain.Ask->ReplyId, true, kStart + std::chrono::milliseconds(2170)));
        CHECK(sight.Sees(kGuest, kMarker, kStart + std::chrono::milliseconds(2180)).Sees);
    }
    THEN("another target is its own question") CHECK(sight.Sees(kGuest, kMarker + 1, kStart).Ask);
    THEN("a stack's reply is not the cache's") CHECK_FALSE(sight.Answer(1234, true, kStart));
}
