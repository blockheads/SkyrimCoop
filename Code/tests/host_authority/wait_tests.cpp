#include "session.h"
#include <Messages/WaitMessages.h>

namespace
{
WaitRequest Request(WaitAction aAction, uint32_t aId = 0, uint32_t aHours = 0, bool aSleeping = false)
{
    WaitRequest request;
    request.Action = aAction;
    request.Id = aId;
    request.Hours = aAction == WaitAction::Start ? aHours : 0;
    request.RemainingSeconds = aAction == WaitAction::Progress ? aHours : 0;
    request.Sleeping = aSleeping;
    return request;
}

uint32_t Start(Session& aSession, ConnectionId_t aRequester = kPeer, bool aSleeping = false)
{
    aSession.Receive(aRequester, Request(WaitAction::Start, 0, 3, aSleeping));
    auto states = aSession.SentTo<NotifyWaitState>(kHost);
    REQUIRE(states.size() == 1);
    REQUIRE(states[0]->Phase == WaitPhase::Preparing);
    return states[0]->Id;
}

void Ready(Session& aSession, uint32_t aId)
{
    for (const auto cGame : {kHost, kPeer, kOtherPeer})
        aSession.Receive(cGame, Request(WaitAction::Ready, aId));
}
} // namespace

TEST_CASE("A shared wait starts only once every game has paused", "[wait]")
{
    Session session;
    const auto cId = Start(session, kPeer, true);
    for (const auto cGame : {kHost, kPeer, kOtherPeer})
    {
        const auto cState = session.SentTo<NotifyWaitState>(cGame).back();
        CHECK(cState->Driver == (cGame == kHost));
        CHECK(cState->Sleeping == (cGame == kHost || cGame == kPeer));
        CHECK(cState->Hours == 3);
    }
    session.Outbox.clear();
    session.Receive(kHost, Request(WaitAction::Ready, cId));
    session.Receive(kHost, Request(WaitAction::Ready, cId));
    session.Receive(kPeer, Request(WaitAction::Ready, cId + 1));
    session.Receive(kPeer, Request(WaitAction::Ready, cId));
    CHECK(session.SentTo<NotifyWaitState>(kHost).empty());
    session.Receive(kOtherPeer, Request(WaitAction::Ready, cId));
    REQUIRE(session.SentTo<NotifyWaitState>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyWaitState>(kHost).back()->Phase == WaitPhase::Running);
}

TEST_CASE("The host's native rest represents a guest's sleep to the party's scripts", "[wait][party-rest]")
{
    Session session;
    const auto cId = Start(session, kOtherPeer, true);
    Ready(session, cId);
    SECTION("The guest's sleep completes")
    {
        session.Receive(kHost, Request(WaitAction::Progress, cId, 0));
    }
    SECTION("Another player cancels the guest's sleep")
    {
        session.Receive(kPeer, Request(WaitAction::Cancel, cId));
    }
    const auto states = session.SentTo<NotifyWaitState>(kHost);
    REQUIRE(states.size() >= 3);
    CHECK(states.front()->Phase == WaitPhase::Preparing);
    CHECK(states[1]->Phase == WaitPhase::Running);
    CHECK(states.back()->Phase == WaitPhase::Finished);
    for (const auto* cpState : states)
    {
        CHECK(cpState->Id == cId);
        CHECK(cpState->Driver);
        CHECK(cpState->Sleeping); // native OK selects 40414; native completion/cancel selects sleep stop
    }
    for (const auto* cpState : session.SentTo<NotifyWaitState>(kOtherPeer))
        CHECK(cpState->Sleeping);
    for (const auto* cpState : session.SentTo<NotifyWaitState>(kPeer))
        CHECK_FALSE(cpState->Sleeping);

    // The next guest wait must use 40415 on the host, with no sleeping state carried over.
    session.Outbox.clear();
    const auto cNext = Start(session, kPeer, false);
    Ready(session, cNext);
    session.Receive(kPeer, Request(WaitAction::Cancel, cNext));
    for (const auto cPlayer : {kHost, kPeer, kOtherPeer})
        for (const auto* cpState : session.SentTo<NotifyWaitState>(cPlayer))
            CHECK_FALSE(cpState->Sleeping);
}

TEST_CASE("Only the host advances a shared wait by the native remaining seconds", "[wait]")
{
    Session session;
    const auto cId = Start(session);
    session.Outbox.clear();
    session.Receive(kHost, Request(WaitAction::Progress, cId, 9900));
    CHECK(session.Outbox.empty());
    Ready(session, cId);
    session.Outbox.clear();
    session.Receive(kPeer, Request(WaitAction::Progress, cId, 9900));
    session.Receive(kHost, Request(WaitAction::Progress, cId + 1, 9900));
    session.Receive(kHost, Request(WaitAction::Progress, cId, 10800));
    session.Receive(kHost, Request(WaitAction::Progress, cId, 0xFFFFFFFF));
    CHECK(session.Outbox.empty());
    session.HostClock.Time = 23.5f;
    session.HostClock.DaysPassed = 200;
    session.Receive(kHost, Request(WaitAction::Progress, cId, 9900));
    auto progress = session.SentTo<NotifyWaitState>(kPeer);
    REQUIRE(progress.size() == 1);
    CHECK(progress.back()->Calendar == session.HostClock);
    CHECK(progress.back()->RemainingSeconds == 9900);
    session.Receive(kHost, Request(WaitAction::Progress, cId, 9900));
    CHECK(session.SentTo<NotifyWaitState>(kPeer).size() == 1);
    session.HostClock.Time = 0.5f;
    ++session.HostClock.DaysPassed;
    // A native step is a quarter hour in AE; completion follows seconds, not the number of callbacks.
    for (uint32_t remaining = 9000; remaining > 0; remaining -= 900)
        session.Receive(kHost, Request(WaitAction::Progress, cId, remaining));
    session.Receive(kHost, Request(WaitAction::Progress, cId, 0));
    progress = session.SentTo<NotifyWaitState>(kPeer);
    REQUIRE(progress.size() == 13);
    CHECK(progress[11]->Phase == WaitPhase::Progress);
    CHECK(progress[11]->RemainingSeconds == 0);
    CHECK(progress[12]->Phase == WaitPhase::Finished);
    CHECK(progress[12]->Calendar.DaysPassed == 201);
}

TEST_CASE("Shared waits reject bad durations and overlapping requests", "[wait]")
{
    Session session;
    for (const uint32_t cHours : {0u, 25u, 0xFFFFFFFFu})
    {
        session.Receive(kPeer, Request(WaitAction::Start, 0, cHours));
        REQUIRE(!session.SentTo<NotifyWaitState>(kPeer).empty());
        CHECK(session.SentTo<NotifyWaitState>(kPeer).back()->Phase == WaitPhase::Refused);
    }
    session.Outbox.clear();
    const auto cId = Start(session);
    session.Outbox.clear();
    session.Receive(kOtherPeer, Request(WaitAction::Start, 0, 24, true));
    CHECK(session.Outbox.empty());
    Ready(session, cId);
    CHECK(session.SentTo<NotifyWaitState>(kHost).back()->Hours == 3);
}

TEST_CASE("Any player can cancel without a stale cancel ending the next wait", "[wait]")
{
    Session session;
    const auto cId = Start(session);
    Ready(session, cId);
    session.Receive(kOtherPeer, Request(WaitAction::Cancel, cId));
    CHECK(session.SentTo<NotifyWaitState>(kHost).back()->Phase == WaitPhase::Finished);
    session.Outbox.clear();
    const auto cNext = Start(session, kHost);
    CHECK(cNext != cId);
    session.Outbox.clear();
    session.Receive(kPeer, Request(WaitAction::Cancel, cId));
    session.Receive(kPeer, Request(WaitAction::Progress, cId, 0));
    CHECK(session.Outbox.empty());
    session.Receive(kPeer, Request(WaitAction::Failed, cNext));
    CHECK(session.SentTo<NotifyWaitState>(kHost).back()->Phase == WaitPhase::Finished);
}

TEST_CASE("A disconnect or a stuck menu releases a shared wait", "[wait]")
{
    Session session;
    session.Frame(100);
    const auto cId = Start(session);
    SECTION("A player leaves")
    {
        session.Leave(kOtherPeer);
    }
    SECTION("A game never opens its menu")
    {
        session.Frame(15100);
    }
    SECTION("The host countdown stops")
    {
        Ready(session, cId);
        session.Frame(15100);
    }
    SECTION("A player's character arrives while the wait is preparing")
    {
        AnnounceCharacterRequest enter;
        enter.SessionId = session.CharacterId(kOtherPeer);
        enter.ReferenceId = GameId(0, 0x14);
        session.Receive(kOtherPeer, enter);
    }
    const auto cStates = session.SentTo<NotifyWaitState>(kHost);
    REQUIRE(!cStates.empty());
    CHECK(cStates.back()->Phase == WaitPhase::Finished);
}

TEST_CASE("The rest world refresh is accepted only for the most recently completed shared wait", "[wait]")
{
    Session session;
    WaitWorldRequest refresh;
    refresh.State.Cell = GameId(0, 0x31D2E);
    refresh.State.Changed = {{GameId(0, 0x1A002), 2}};
    session.LoadedCells[refresh.State.Cell.BaseId] = {
        {.Type = ObjectChange::Kind::kRecord, .Object = GameId(0, 0x1A002), .Value = 0}};
    session.Receive(kPeer, refresh);
    CHECK(session.SentTo<NotifyWaitWorld>(kPeer).empty());
    refresh.Id = Start(session);
    session.Receive(kPeer, refresh);
    CHECK(session.SentTo<NotifyWaitWorld>(kPeer).empty());
    session.Receive(kPeer, Request(WaitAction::Cancel, refresh.Id));
    session.Outbox.clear();
    auto stale = refresh;
    ++stale.Id;
    session.Receive(kPeer, stale);
    session.Receive(kHost, refresh);
    CHECK(session.Outbox.empty());
    session.Receive(kPeer, refresh);
    const auto replies = session.SentTo<NotifyWaitWorld>(kPeer);
    REQUIRE(replies.size() == 1);
    CHECK(replies[0]->Id == refresh.Id);
    CHECK(replies[0]->Last);
    CHECK(replies[0]->State.Cell == refresh.State.Cell);
    CHECK(replies[0]->State.Changes == session.LoadedCells[refresh.State.Cell.BaseId]);
    CHECK(session.GuestChanged == refresh.State.Changed);
    CHECK(session.SentTo<NotifyObjectChanges>(kPeer).empty());
    CHECK(session.SentTo<NotifyWaitWorld>(kOtherPeer).empty());
    session.Outbox.clear();
    const auto next = Start(session);
    session.Receive(kPeer, Request(WaitAction::Cancel, next));
    session.Outbox.clear();
    session.Receive(kPeer, refresh);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Shared rest refreshes split large cells and finish empty or unloaded cells", "[wait]")
{
    Session session;
    WaitWorldRequest refresh;
    refresh.Id = Start(session);
    session.Receive(kPeer, Request(WaitAction::Cancel, refresh.Id));
    session.Outbox.clear();
    refresh.State.Cell = GameId(0, 0x31D2E);
    SECTION("A large cell")
    {
        auto& rows = session.LoadedCells[refresh.State.Cell.BaseId];
        for (uint32_t i = 0; i < 2050; ++i)
            rows.push_back({.Type = ObjectChange::Kind::kRecord, .Object = GameId(0, 0x1000 + i), .Value = i});
        session.Receive(kPeer, refresh);
        const auto replies = session.SentTo<NotifyWaitWorld>(kPeer);
        REQUIRE(replies.size() == 3);
        std::vector<ObjectChange> received;
        for (size_t i = 0; i < replies.size(); ++i)
        {
            CHECK(replies[i]->Id == refresh.Id);
            CHECK(replies[i]->Last == (i + 1 == replies.size()));
            CHECK(replies[i]->State.Changes.size() <= 1024);
            const auto& chunk = replies[i]->State.Changes;
            received.insert(received.end(), chunk.begin(), chunk.end());
        }
        CHECK(received == rows);
    }
    SECTION("A loaded cell with no changed rows")
    {
        session.LoadedCells[refresh.State.Cell.BaseId] = {};
        session.Receive(kPeer, refresh);
        const auto replies = session.SentTo<NotifyWaitWorld>(kPeer);
        REQUIRE(replies.size() == 1);
        CHECK(replies[0]->Last);
        CHECK(replies[0]->State.Changes.empty());
    }
    SECTION("A cell not loaded by the host")
    {
        session.Receive(kPeer, refresh);
        const auto replies = session.SentTo<NotifyWaitWorld>(kPeer);
        REQUIRE(replies.size() == 1);
        CHECK(replies[0]->Last);
        CHECK(replies[0]->State.Changes.empty());
    }
}
