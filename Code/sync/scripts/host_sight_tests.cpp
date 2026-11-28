// The host's copy of each guest's sight (docs/re/systems/papyrus-guest-scripts.md, "A player's line of sight").
#include <catch2/catch.hpp>

#include <scripts/HostSight.h>

namespace
{
constexpr uint32_t kGuest = 0xFF000835;
constexpr uint32_t kMarker = 0x709D4;
const HostSight::Clock::time_point kStart{};

HostSight::Clock::time_point At(int aMilliseconds)
{
    return kStart + std::chrono::milliseconds(aMilliseconds);
}
} // namespace

SCENARIO("The host reads a guest's sight from the guest's game", "[sight]")
{
    HostSight sight;
    const auto cFirst = sight.Sees(kGuest, kMarker, kStart);

    THEN("until its game answers there is no answer, and one question goes out")
    {
        CHECK_FALSE(cFirst.Sees);
        REQUIRE(cFirst.Ask);
        CHECK(cFirst.Ask->Guest == kGuest);
        CHECK(cFirst.Ask->Target == kMarker);
        CHECK(HostSight::IsSightReply(cFirst.Ask->ReplyId));
        CHECK_FALSE(sight.Sees(kGuest, kMarker, At(20)).Ask);
    }
    WHEN("its game answers yes")
    {
        REQUIRE(sight.Answer(cFirst.Ask->ReplyId, true, At(50)));
        const auto cThen = sight.Sees(kGuest, kMarker, At(60));
        THEN("it sees, and the next question goes out") CHECK((cThen.Sees == true && cThen.Ask));
        THEN("an answer older than a second is no answer, not a no") CHECK_FALSE(sight.Sees(kGuest, kMarker, At(1100)).Sees);
    }
    WHEN("its game answers no")
    {
        REQUIRE(sight.Answer(cFirst.Ask->ReplyId, false, At(50)));
        THEN("it is a no") CHECK(sight.Sees(kGuest, kMarker, At(60)).Sees == false);
    }
    WHEN("its game never answers (it left)")
    THEN("the question goes out again after two seconds") CHECK(sight.Sees(kGuest, kMarker, At(2100)).Ask);
    WHEN("an answer comes for a question asked again since")
    {
        const auto cAgain = sight.Sees(kGuest, kMarker, At(2100));
        REQUIRE(sight.Answer(cFirst.Ask->ReplyId, true, At(2150)));
        THEN("only the newer question's answer counts") CHECK_FALSE(sight.Sees(kGuest, kMarker, At(2160)).Sees);
        REQUIRE(sight.Answer(cAgain.Ask->ReplyId, true, At(2170)));
        CHECK(sight.Sees(kGuest, kMarker, At(2180)).Sees == true);
    }
    THEN("another target is its own question") CHECK(sight.Sees(kGuest, kMarker + 1, kStart).Ask);
    THEN("a stack's reply is not the cache's") CHECK_FALSE(sight.Answer(1234, true, kStart));
}

// 54037 sends OnLostLOS / OnGainLOS when 2 - HasLOS differs from the registration's last state (read in IDA).
SCENARIO("A LOS registration's poll with no answer yet sends no event", "[sight]")
{
    THEN("a registration never polled: not seen, kept as its new state, so a later yes or no is a real change")
    {
        CHECK(HostSight::Unknown(0).Sees == false);
        CHECK(HostSight::Unknown(0).LastState == 2);
    }
    THEN("a seen one stays seen, an unseen one unseen")
    {
        CHECK((HostSight::Unknown(1).Sees && HostSight::Unknown(1).LastState == 1));
        CHECK((!HostSight::Unknown(2).Sees && HostSight::Unknown(2).LastState == 2));
    }
}
