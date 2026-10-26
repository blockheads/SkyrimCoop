#include "FakeWait.h"
#include "Party.h"
#include <Events/GameLoadedEvent.h>
#include <Messages/WaitMessages.h>

namespace
{
void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x00031D2E;
    aEngine.LoadCell(0x00031D2E);
    aEngine.Calendar.TimeScale = 0;
    aEngine.Calendar.Time = 22.5f;
}
FakeWait& Wait(World& aGame)
{
    return aGame.ctx().at<FakeWait&>();
}
} // namespace

SCENARIO("Either player starts a shared native wait across midnight", "[wait][world]")
{
    Party party(1, Plugins);
    party.Until(
        [&] {
            return !Party::Engine(party.Host()).CopiesOf().empty() && !Party::Engine(party.Guest()).CopiesOf().empty();
        },
        "both player characters have entered");
    World* pStarter = &party.Host();
    bool sleeping = false;
    uint32_t tickSeconds = 900;
    SECTION("The host waits")
    {
    }
    SECTION("The guest waits")
    {
        pStarter = &party.Guest();
    }
    SECTION("The host sleeps")
    {
        sleeping = true;
    }
    SECTION("The guest sleeps")
    {
        pStarter = &party.Guest();
        sleeping = true;
    }
    SECTION("A different native step size")
    {
        tickSeconds = 1800;
    }
    auto& host = Wait(party.Host());
    auto& guest = Wait(party.Guest());
    host.TickSeconds = guest.TickSeconds = tickSeconds;
    const uint32_t cSteps = 3 * 3600 / tickSeconds;
    party.As(*pStarter, [&] { REQUIRE(Wait(*pStarter).Confirm(3, sleeping)); });
    party.Until([&] { return host.Running && guest.Running; },
                "every native menu is open and the host starts the countdown");
    CHECK(host.Open);
    CHECK(guest.Open);
    CHECK(host.Driver);
    CHECK_FALSE(guest.Driver);
    CHECK(host.Sleeping == sleeping); // the host's scripts see the party's sleep, whoever used the bed
    CHECK(guest.Sleeping == (sleeping && pStarter == &party.Guest()));
    const auto cBefore = Party::Engine(party.Host()).Calendar.DaysPassed;
    for (uint32_t step = 1; step <= cSteps; ++step)
    {
        party.As(party.Host(), [&] { host.Step(); });
        party.Until([&] { return guest.Steps == step; }, "the guest consumes exactly the host's next native step");
        const auto& cHost = Party::Engine(party.Host()).Calendar;
        const auto& cGuest = Party::Engine(party.Guest()).Calendar;
        CHECK(cHost == cGuest);
        CHECK(host.RemainingSeconds == 3 * 3600 - step * tickSeconds);
        CHECK(guest.RemainingSeconds == host.RemainingSeconds);
        if (step < cSteps)
        {
            CHECK(host.Open);
            CHECK(guest.Open);
        }
    }
    party.Until([&] { return !host.Open && !guest.Open; }, "the party's menus close together");
    CHECK(Party::Engine(party.Host()).Calendar.DaysPassed == cBefore + 1);
    CHECK(Party::Engine(party.Host()).Calendar.Time == Approx(1.5f));
    CHECK(host.Starts == 1);
    CHECK(guest.Starts == 1);
}

SCENARIO("A slow guest keeps the host behind the menu readiness barrier", "[wait]")
{
    Party party(1, Plugins);
    party.Until(
        [&] {
            return !Party::Engine(party.Host()).CopiesOf().empty() && !Party::Engine(party.Guest()).CopiesOf().empty();
        },
        "both player characters have entered");
    auto& host = Wait(party.Host());
    auto& guest = Wait(party.Guest());
    guest.Loaded = false;
    party.As(party.Host(), [&] { host.Confirm(2); });
    party.Until([&] { return host.Open && guest.Open; }, "both games are preparing");
    party.As(party.Host(), [&] { host.Step(); });
    CHECK_FALSE(host.Running);
    CHECK_FALSE(guest.Running);
    CHECK(host.Steps == 0);
    CHECK(guest.Steps == 0);
    guest.Loaded = true;
    party.Until([&] { return host.Running && guest.Running; }, "the last ready game releases the countdown");
}

SCENARIO("A shared wait is released on cancellation or engine refusal", "[wait]")
{
    Party party(1, Plugins);
    party.Until(
        [&] {
            return !Party::Engine(party.Host()).CopiesOf().empty() && !Party::Engine(party.Guest()).CopiesOf().empty();
        },
        "both player characters have entered");
    auto& host = Wait(party.Host());
    auto& guest = Wait(party.Guest());
    SECTION("Another player's game cannot wait")
    {
        host.Available = false;
        party.As(party.Guest(), [&] { guest.Confirm(3, true); });
        party.Until([&] { return guest.Prepares > 0; }, "the request reaches the party");
    }
    SECTION("The guest cancels the host's wait")
    {
        party.As(party.Host(), [&] { host.Confirm(3); });
        party.Until([&] { return host.Running && guest.Running; }, "both games started");
        party.As(party.Guest(), [&] { CHECK(guest.Cancel()); });
    }
    SECTION("The initiator cancels while the request is still in flight")
    {
        party.As(party.Guest(), [&] {
            guest.Confirm(3);
            CHECK(guest.Cancel());
        });
        party.Until([&] { return host.Prepares > 0; }, "the host receives the pending request");
    }
    SECTION("The guest leaves during a wait")
    {
        party.As(party.Host(), [&] { host.Confirm(3); });
        party.Until([&] { return host.Running && guest.Running; }, "both games started");
        party.Leave(party.Guest());
    }
    SECTION("A game loads while waiting")
    {
        party.As(party.Host(), [&] { host.Confirm(3); });
        party.Until([&] { return host.Running && guest.Running; }, "both games started");
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(GameLoadedEvent{}); });
    }
    party.Until([&] { return !host.Open && !guest.Open; }, "all shared menus release their pause");
    CHECK(host.Steps == 0);
    CHECK(guest.Steps == 0);
}

SCENARIO("A shared wait holds the guest's clock between host steps", "[wait][world]")
{
    Party party(1, Plugins);
    party.Until(
        [&] {
            return !Party::Engine(party.Host()).CopiesOf().empty() && !Party::Engine(party.Guest()).CopiesOf().empty();
        },
        "both player characters have entered");
    auto& host = Wait(party.Host());
    auto& guest = Wait(party.Guest());
    // A large time scale makes even a short projection of wall time observable.
    Party::Engine(party.Host()).Calendar.TimeScale = 1000000.f;
    party.As(party.Host(), [&] { host.Confirm(3); });
    party.Until([&] { return host.Running && guest.Running; }, "every menu has paused");
    const auto cBefore = Party::Engine(party.Host()).Calendar;
    // Run only guest frames: its own engine still ticks, but GuestClock must reapply the frozen host time.
    for (uint32_t frame = 0; frame < 200; ++frame)
        party.Step(party.Guest());
    CHECK(guest.Steps == 0);
    CHECK(Party::Engine(party.Guest()).Calendar.Time == Approx(cBefore.Time));
}

SCENARIO("Four players share one countdown and a departed guest gets solo waiting back", "[wait]")
{
    Party party(3, Plugins);
    party.Until(
        [&] {
            return Party::Engine(party.Host()).CopiesOf().size() == 3 &&
                   Party::Engine(party.Guest(0)).CopiesOf().size() == 3 &&
                   Party::Engine(party.Guest(1)).CopiesOf().size() == 3 &&
                   Party::Engine(party.Guest(2)).CopiesOf().size() == 3;
        },
        "all four characters have entered");
    auto& last = Wait(party.Guest(2));
    last.Loaded = false;
    party.As(party.Guest(), [&] { Wait(party.Guest()).Confirm(1, true); });
    party.Until([&] { return last.Open && Wait(party.Host()).Open; }, "the party opens the menus");
    CHECK_FALSE(Wait(party.Host()).Running);
    last.Loaded = true;
    party.Until([&] { return last.Running && Wait(party.Host()).Running; }, "the fourth player releases the barrier");
    for (uint32_t step = 0; step < 4; ++step)
        party.As(party.Host(), [&] { Wait(party.Host()).Step(); });
    party.Until(
        [&] {
            return !last.Open && !Wait(party.Host()).Open && !Wait(party.Guest()).Open && !Wait(party.Guest(1)).Open;
        },
        "every game finishes the one hour");
    CHECK(last.Steps == 4);
    CHECK(Wait(party.Guest(1)).Steps == 4);
    CHECK(Wait(party.Guest()).Steps == 4);
    party.Leave(party.Guest(2));
    party.As(party.Guest(2), [&] { CHECK_FALSE(last.Confirm(3)); });
}

SCENARIO("A guest consumes each native step when several arrive in one frame", "[wait]")
{
    Party party(1, Plugins);
    party.Until(
        [&] {
            return !Party::Engine(party.Host()).CopiesOf().empty() && !Party::Engine(party.Guest()).CopiesOf().empty();
        },
        "both players entered");
    auto& host = Wait(party.Host());
    auto& guest = Wait(party.Guest());
    party.As(party.Host(), [&] { host.Confirm(3); });
    party.Until([&] { return host.Running && guest.Running; }, "both menus started");
    party.As(party.Host(), [&] {
        for (uint32_t step = 0; step < 4; ++step)
            host.Step();
    });
    party.Until([&] { return guest.Steps == 4; }, "the guest consumes four quarter-hour native steps");
    CHECK(guest.RemainingSeconds == 7200);
    CHECK(host.RemainingSeconds == 7200);
    CHECK(guest.Open);
    CHECK(host.Open);
    CHECK(Party::Engine(party.Guest()).Calendar == Party::Engine(party.Host()).Calendar);
}

SCENARIO("A guest takes the host's cell rows again after a shared wait", "[wait][world]")
{
    constexpr uint32_t cDoor = 0x1A002;
    Party party(1, [cDoor](FakeEngine& aEngine) {
        Plugins(aEngine);
        aEngine.References[cDoor] = {.Cell = 0x31D2E, .Locked = true, .LockLevel = 25};
    });
    party.Until(
        [&] {
            return !Party::Engine(party.Host()).CopiesOf().empty() && !Party::Engine(party.Guest()).CopiesOf().empty();
        },
        "both players entered");
    auto& host = Wait(party.Host());
    auto& guest = Wait(party.Guest());
    party.As(party.Host(), [&] { host.Confirm(1); });
    party.Until([&] { return host.Running && guest.Running; }, "both menus started");
    // An engine reset during fast-forward changes a local row, not a player's proposal.
    Party::Engine(party.Guest()).SetLock(cDoor, false, 0);
    REQUIRE_FALSE(Party::Engine(party.Guest()).References[cDoor].Locked);
    SECTION("The countdown completes")
    {
        for (uint32_t step = 0; step < 4; ++step)
            party.As(party.Host(), [&] { host.Step(); });
    }
    SECTION("A player cancels")
    {
        party.As(party.Guest(), [&] { guest.Cancel(); });
    }
    party.Until([&] { return !guest.Open && Party::Engine(party.Guest()).References[cDoor].Locked; },
                "closing the shared wait restores the host's cell rows");
    CHECK(party.Differences().empty());
}

SCENARIO("Only the completed shared rest defers its own record flags", "[wait][world]")
{
    constexpr uint32_t cObject = 0x1A002;
    Party party(1, [cObject](FakeEngine& aEngine) {
        Plugins(aEngine);
        aEngine.References[cObject] = {.Cell = 0x31D2E};
    });
    party.Until([&] { return !Party::Engine(party.Guest()).CopiesOf().empty(); }, "both players entered");
    struct Trace
    {
        uint32_t Id{};
        void OnState(const NotifyWaitState& acState)
        {
            if (acState.Phase == WaitPhase::Preparing)
                Id = acState.Id;
        }
    } trace;
    entt::scoped_connection connection = party.Guest().GetDispatcher().sink<NotifyWaitState>().connect<&Trace::OnState>(trace);
    party.As(party.Host(), [&] { Wait(party.Host()).Confirm(1); });
    party.Until([&] { return Wait(party.Guest()).Running; }, "the shared menu started");
    party.As(party.Guest(), [&] { Wait(party.Guest()).Cancel(); });
    // Stop as soon as the guest sends its refresh request, before another host frame can answer it.
    party.Until([&] { return !Wait(party.Guest()).Open; }, "the shared menu closed");
    REQUIRE(trace.Id != 0);
    auto& engine = Party::Engine(party.Guest());
    engine.References[cObject].RecordFlags = 2;
    engine.PendingPlacements.insert(cObject);
    NotifyWaitWorld reply;
    reply.Id = trace.Id;
    reply.Last = false;
    reply.State.Cell = SessionIdOf(0x31D2E);
    reply.State.Changes = {{.Type = ObjectChange::Kind::kRecord, .Object = SessionIdOf(cObject), .Value = 0}};
    party.As(party.Guest(), [&] {
        auto stale = reply;
        ++stale.Id;
        party.Guest().GetDispatcher().trigger(stale);
        auto unsolicited = reply;
        unsolicited.State.Cell = SessionIdOf(0x31D2F);
        party.Guest().GetDispatcher().trigger(unsolicited);
    });
    party.Step(party.Guest());
    CHECK(engine.References[cObject].RecordFlags == 2);
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(reply); });
    engine.PendingPlacements.clear();
    party.Step(party.Guest());
    CHECK(engine.References[cObject].RecordFlags == 2); // remaining chunks may still contain placements
    engine.PendingPlacements.insert(cObject);
    reply.Last = true;
    reply.State.Changes.clear();
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(reply); });
    party.Step(party.Guest());
    CHECK(engine.References[cObject].RecordFlags == 2); // complete snapshot, placement still pending

    uint32_t expected = 0;
    SECTION("The rest's flags apply after its placement finishes") {}
    SECTION("An ordinary update applies immediately and supersedes the queued rest flags")
    {
        NotifyObjectChanges changes;
        changes.Changes = {{.Type = ObjectChange::Kind::kRecord, .Object = SessionIdOf(cObject), .Value = 8}};
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(changes); });
        CHECK(engine.References[cObject].RecordFlags == 8);
        expected = 8;
    }
    SECTION("A new shared rest discards the old snapshot")
    {
        NotifyWaitState next;
        next.Id = trace.Id + 1;
        next.Phase = WaitPhase::Preparing;
        next.Hours = 1;
        next.RemainingSeconds = 3600;
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(next); });
        expected = 2;
    }
    SECTION("A world load discards the old snapshot")
    {
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(GameLoadedEvent{}); });
        expected = 2;
    }
    engine.PendingPlacements.clear();
    party.Step(party.Guest());
    CHECK(engine.References[cObject].RecordFlags == expected);
    // The cell reply was consumed: another copy cannot start a second flag write.
    reply.State.Changes = {{.Type = ObjectChange::Kind::kRecord, .Object = SessionIdOf(cObject), .Value = 32}};
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(reply); });
    party.Step(party.Guest());
    CHECK(engine.References[cObject].RecordFlags == expected);
}
