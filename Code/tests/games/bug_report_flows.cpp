// A player's /bug (docs/design/bug-reports.md): every game writes its part under the host's one id.
#include "FakeBugReports.h"
#include "Party.h"

#include <Services/BugReportService.h>

SCENARIO("A guest's /bug: every game writes its part under one id", "[bug_report]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();
    auto& hostParts = host.ctx().at<FakeBugReports&>().Parts;
    auto& guestParts = guest.ctx().at<FakeBugReports&>().Parts;

    party.As(guest, [&] { guest.ctx().at<BugReportService>().Report("the bandit froze"); });
    party.Until([&] { return !hostParts.empty() && !guestParts.empty(); }, "both games write their part");

    REQUIRE(hostParts.size() == 1);
    REQUIRE(guestParts.size() == 1);
    CHECK(hostParts[0].Id == guestParts[0].Id);
    CHECK(hostParts[0].Text == "the bandit froze");
    CHECK(guestParts[0].Text == "the bandit froze");
    CHECK(hostParts[0].By == guestParts[0].By);
    THEN("the host's part is the one with the save")
    {
        CHECK(hostParts[0].Host);
        CHECK_FALSE(guestParts[0].Host);
    }
}

SCENARIO("/export: this game's files, named after its player, and a line saying where", "[bug_report]")
{
    Party party;
    World& guest = party.Guest();

    party.As(guest, [&] { guest.ctx().at<BugReportService>().Export(); });
    party.Until([&] { return !guest.ctx().at<FakeBugReports&>().Exports.empty(); }, "the guest exports");
    CHECK(guest.ctx().at<FakeBugReports&>().Exports.size() == 1);
}
