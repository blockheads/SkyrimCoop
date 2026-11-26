// PartyArrivals: players arriving at one location are one story event, the first's (party-model.md rule 4).
#include <catch2/catch.hpp>

#include <Misc/PartyArrivals.h>

namespace
{
constexpr uint32_t kHost = 0x14;       // the host's player
constexpr uint32_t kGuest = 0xFF000801; // a guest's character on the host
constexpr uint32_t kOther = 0xFF000802; // another guest's
constexpr uint32_t kMare = 0x18A56;     // the Bannered Mare
constexpr uint32_t kWhiterun = 0x18A55;
const auto cParty = [](uint32_t) { return true; };
} // namespace

TEST_CASE("Players arriving at one location are one story event", "[arrivals]")
{
    PartyArrivals arrivals;

    // Whoever arrives first is the party's arrival; the others there already had it.
    CHECK(arrivals.Arrive(kGuest, kMare, cParty));
    CHECK_FALSE(arrivals.Arrive(kHost, kMare, cParty));
    CHECK_FALSE(arrivals.Arrive(kOther, kMare, cParty));

    // Leaving and coming back while another stays: the party never left.
    CHECK(arrivals.Arrive(kGuest, kWhiterun, cParty));
    CHECK_FALSE(arrivals.Arrive(kGuest, kMare, cParty));

    // Everyone gone from it: the next arrival is the party's again.
    CHECK(arrivals.Arrive(kHost, kWhiterun, cParty));
    CHECK_FALSE(arrivals.Arrive(kOther, kWhiterun, cParty));
    CHECK_FALSE(arrivals.Arrive(kGuest, kWhiterun, cParty));
    CHECK(arrivals.Arrive(kGuest, kMare, cParty));
}

TEST_CASE("A player who left the session holds no location", "[arrivals]")
{
    PartyArrivals arrivals;
    CHECK(arrivals.Arrive(kGuest, kMare, cParty));
    const auto cWithoutGuest = [](uint32_t aId) { return aId != kGuest; };
    CHECK(arrivals.Arrive(kHost, kMare, cWithoutGuest));
}
