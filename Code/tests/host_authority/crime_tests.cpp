#include "session.h"
#include <Messages/CrimeMessages.h>

namespace
{
CrimeRequest Theft(uint32_t aSequence = 1)
{
    CrimeRequest request;
    request.Sequence = aSequence;
    request.Action = CrimeAction::Theft;
    request.Target = GameId(0, 0x1234);
    request.Item = GameId(0, 0xF);
    request.Count = 5;
    return request;
}

void Enter(Session& aSession, ConnectionId_t aPlayer)
{
    AnnounceCharacterRequest announce;
    announce.SessionId = aSession.CharacterId(aPlayer);
    announce.ReferenceId = GameId(0, 0x14);
    aSession.Receive(aPlayer, announce);
    aSession.Outbox.clear();
}
} // namespace

TEST_CASE("Crime proposals identify the authenticated player's character", "[crime]")
{
    Session session;
    session.Receive(kPeer, Theft()); // joined, but no character yet
    CHECK(session.Crimes.empty());
    Enter(session, kPeer);
    session.Receive(kPeer, Theft());
    REQUIRE(session.Crimes.size() == 1);
    CHECK(session.Crimes[0].first == session.CharacterId(kPeer));
    CHECK(session.Crimes[0].second.Count == 5);
    session.Receive(kPeer, Theft());
    session.Receive(kPeer, Theft(0));
    session.Receive(kHost, Theft(2)); // native host crimes do not need a proposal
    session.Receive(99, Theft(2));
    CHECK(session.Crimes.size() == 1);
    session.Receive(kPeer, Theft(2));
    CHECK(session.Crimes.size() == 2);
}

TEST_CASE("Only host judgment changes bounty for a proposed guest crime", "[crime]")
{
    Session session;
    Enter(session, kPeer);
    bool witnessed = false;
    session.CrimeJudge = [&](uint32_t, const CrimeRequest&) {
        if (witnessed)
            session.Bounty = {{GameId(0, 0x28170), 0, 5}};
    };
    session.Receive(kPeer, Theft());
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).empty());
    witnessed = true;
    session.Receive(kPeer, Theft(2));
    REQUIRE(session.SentTo<NotifyCrimeState>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).back()->Gold == session.Bounty);
    CHECK(session.SentTo<NotifyCrimeState>(kOtherPeer).back()->Gold == session.Bounty);
    CHECK(session.SentTo<NotifyCrimeState>(kHost).empty());
    session.Receive(kPeer, Theft(2));
    CHECK(session.Crimes.size() == 2);
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).size() == 1);
}

TEST_CASE("Host-native bounty, violent buckets and mod factions reach guests", "[crime]")
{
    Session session;
    session.Bounty = {{GameId(0, 0x28170), 1000, 25}, {GameId(0xFE, 0x100812), 50, 10}};
    session.Frame(1);
    REQUIRE(session.SentTo<NotifyCrimeState>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).back()->Gold == session.Bounty);
    session.Frame(2);
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).size() == 1);
    session.Bounty[0].Violent = 0;
    session.Bounty[1].NonViolent = 0;
    session.Frame(3);
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).back()->Gold == session.Bounty);
    session.Bounty.clear(); // pardon/clear after a native resolution
    session.Frame(4);
    REQUIRE(session.SentTo<NotifyCrimeState>(kPeer).size() == 3);
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).back()->Gold.empty());
}

TEST_CASE("Party criminal-enemy flags synchronize independently of bounty amounts", "[crime]")
{
    Session session;
    session.Bounty = {{GameId(0, 0x28170), 0, 0, 0, 0, true}};
    session.Frame(1);
    REQUIRE(session.SentTo<NotifyCrimeState>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).back()->Gold.front().PlayerEnemy);
    session.Bounty.front().PlayerEnemy = false;
    session.Frame(2);
    REQUIRE(session.SentTo<NotifyCrimeState>(kPeer).size() == 2);
    CHECK_FALSE(session.SentTo<NotifyCrimeState>(kPeer).back()->Gold.front().PlayerEnemy);
    session.Bounty.clear();
    session.Frame(3);
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).back()->Gold.empty());
}

TEST_CASE("A joining character receives even an empty authoritative crime snapshot", "[crime]")
{
    Session session;
    SECTION("No bounty")
    {
    }
    SECTION("Existing bounty")
    {
        session.Bounty = {{GameId(0, 0x28170), 1000, 0}};
    }
    SECTION("Criminal hostility without bounty")
    {
        session.Bounty = {{GameId(0, 0x28170), 0, 0, 0, 0, true}};
    }
    Enter(session, kPeer);
    // Enter clears its outbox: announce again to exercise the ordinary cell-entry refresh.
    AnnounceCharacterRequest announce;
    announce.SessionId = session.CharacterId(kPeer);
    announce.ReferenceId = GameId(0, 0x14);
    session.Receive(kPeer, announce);
    REQUIRE(session.SentTo<NotifyCrimeState>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyCrimeState>(kPeer).back()->Gold == session.Bounty);
}

TEST_CASE("Malformed or repeated fine and theft proposals cannot execute twice", "[crime]")
{
    Session session;
    Enter(session, kPeer);
    auto request = Theft();
    for (auto count : {0u, 0x80000000u, 0xFFFFFFFFu})
    {
        request.Count = count;
        session.Receive(kPeer, request);
    }
    request.Action = static_cast<CrimeAction>(255);
    session.Receive(kPeer, request);
    CHECK(session.Crimes.empty());
    request.Action = CrimeAction::PayFine;
    request.GoToJail = true;
    request.RemoveStolenItems = true;
    session.Receive(kPeer, request);
    session.Receive(kPeer, request);
    REQUIRE(session.Crimes.size() == 1);
    CHECK(session.Crimes[0].second.GoToJail);
    CHECK(session.Crimes[0].second.RemoveStolenItems);
}

TEST_CASE("Only the caught lockpicker receives the host's native decision", "[crime]")
{
    Session session;
    Enter(session, kPeer);
    auto request = Theft();
    request.Action = CrimeAction::Lockpick;
    request.Count = 19; // one menu attempt, not an amount of crime gold
    session.Receive(kPeer, request);
    CHECK(session.SentTo<NotifyLockCrime>(kPeer).empty());
    session.CrimeCaught = true;
    request.Sequence = 2;
    session.Receive(kPeer, request);
    REQUIRE(session.SentTo<NotifyLockCrime>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyLockCrime>(kPeer).back()->Attempt == 19);
    CHECK(session.SentTo<NotifyLockCrime>(kOtherPeer).empty());
    CHECK(session.SentTo<NotifyLockCrime>(kHost).empty());
    session.Receive(kPeer, request);
    CHECK(session.Crimes.size() == 2);
    CHECK(session.SentTo<NotifyLockCrime>(kPeer).size() == 1);
    request.Sequence = 3;
    session.Receive(kPeer, request); // another native check in the same menu, already charged
    CHECK(session.Crimes.size() == 2);
    CHECK(session.SentTo<NotifyLockCrime>(kPeer).size() == 2);
}

TEST_CASE("Container crime proposals preserve item ownership for host valuation", "[crime]")
{
    Session session;
    Enter(session, kPeer);
    auto request = Theft();
    request.Action = CrimeAction::ContainerTheft;
    session.Receive(kPeer, request);
    CHECK(session.Crimes.empty());
    CrimeItem line;
    line.Item.BaseId = GameId(0, 0x64B3D);
    line.Item.Count = 3;
    line.Owners.push_back({GameId(0, 0x13479), 3});
    line.Taking = true;
    request.Items.push_back(line);
    session.Receive(kPeer, request);
    REQUIRE(session.Crimes.size() == 1);
    const auto& judged = session.Crimes[0].second.Items;
    REQUIRE(judged.size() == 1);
    CHECK(judged[0].Item.BaseId == line.Item.BaseId);
    CHECK(judged[0].Item.Count == 3);
    CHECK(judged[0].Taking);
    REQUIRE(judged[0].Owners.size() == 1);
    CHECK(judged[0].Owners[0].Owner == line.Owners[0].Owner);
    CHECK(judged[0].Owners[0].Count == 3);
    session.Receive(kPeer, request);
    CHECK(session.Crimes.size() == 1);
    request.Sequence = 2;
    request.Items[0].Item.Count = -1;
    session.Receive(kPeer, request);
    CHECK(session.Crimes.size() == 1);
    request.Items[0].Item.Count = 3;
    for (auto count : {0u, 0x8000u, 0x10000u, 0xFFFFFFFFu})
    {
        request.Items[0].Owners[0].Count = count;
        session.Receive(kPeer, request);
    }
    CHECK(session.Crimes.size() == 1);
    request.Items[0].Owners.assign(CrimeRequest::kMaxEntries, {GameId{}, 1});
    session.Receive(kPeer, request);
    CHECK(session.Crimes.size() == 1);
}
