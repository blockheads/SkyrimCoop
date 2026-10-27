#include "session.h"
#include <Messages/DeathMessages.h>

namespace
{
using namespace HostAuthorityTest;
constexpr uint32_t kFollower = 0xA2C94;

void Body(Session& aSession, ConnectionId_t aConnection)
{
    AnnounceCharacterRequest body;
    body.SessionId = aSession.CharacterId(aConnection);
    body.ReferenceId = GameId(0, 0x14);
    body.CellId = GameId(0, 0x31D2E);
    aSession.Receive(aConnection, body);
    if (aConnection == kHost)
    {
        NotifyCharacterSpawn own;
        own.SessionId = body.SessionId;
        own.CellId = body.CellId;
        own.IsPlayer = true;
        aSession.Spawns.push_back(own);
    }
    PlayerDeathRequest alive;
    alive.SessionId = body.SessionId;
    aSession.Receive(aConnection, alive);
}

void Ready(Session& aSession)
{
    Body(aSession, kHost);
    Body(aSession, kPeer);
    Body(aSession, kOtherPeer);
    aSession.Followers = std::vector<Followers::State>{{kFollower, {0, 0x31D2E, {}}, false, false}};
}

uint32_t Target(const Session& aSession)
{
    return aSession.FollowerTargets.empty() ? 0 : aSession.FollowerTargets.front().Character;
}
} // namespace

TEST_CASE("Followers: native recruitment and Follow choose their picker; other updates retain the anchor",
          "[HostAuthority][followers]")
{
    Session session(true);
    Ready(session);
    session.FollowerOrders.push_back({kFollower, session.CharacterId(kPeer), Followers::Command::Recruit});
    session.Frame(1);
    CHECK(Target(session) == session.CharacterId(kPeer));
    session.Frame(2); // trading or favors do not emit a Follow command
    CHECK(session.FollowerBindings.Desired(kFollower) == "guest");
    session.FollowerOrders.push_back({kFollower, session.CharacterId(kOtherPeer), Followers::Command::Follow});
    session.Frame(3);
    CHECK(Target(session) == session.CharacterId(kOtherPeer));
}

TEST_CASE("Followers: death and disconnect use the standing host; stable keys restore a rejoined member",
          "[HostAuthority][followers]")
{
    Session session(true);
    Ready(session);
    session.FollowerBindings.Follow(kFollower, "guest");
    session.Frame(1);
    CHECK(Target(session) == session.CharacterId(kPeer));
    PlayerDeathRequest dead;
    dead.SessionId = session.CharacterId(kPeer);
    dead.Generation = 1;
    dead.Down = true;
    session.Receive(kPeer, dead);
    session.Frame(2);
    CHECK(Target(session) == session.CharacterId(kHost));
    CHECK(session.FollowerBindings.Desired(kFollower) == "guest");
    session.Leave(kPeer);
    session.Frame(3);
    CHECK(Target(session) == session.CharacterId(kHost));
    auto rejoin = JoinRequest("Lydia", 12);
    rejoin.PlayerKey = "guest";
    session.Join(kPeer, rejoin);
    Body(session, kPeer);
    session.Frame(4);
    CHECK(Target(session) == session.CharacterId(kPeer));
}

TEST_CASE("Followers: waiting suppresses following and clearing an alias forgets the binding",
          "[HostAuthority][followers]")
{
    Session session(true);
    Ready(session);
    session.FollowerBindings.Follow(kFollower, "guest");
    session.Followers->front().Waiting = true;
    session.FollowerOrders.push_back({kFollower, session.CharacterId(kHost), Followers::Command::Wait});
    session.Frame(1);
    CHECK(Target(session) == 0);
    CHECK(session.FollowerBindings.Desired(kFollower) == "guest");
    session.Followers->front().Waiting = false;
    session.FollowerOrders.push_back({kFollower, session.CharacterId(kHost), Followers::Command::Follow});
    session.Frame(2);
    CHECK(Target(session) == session.CharacterId(kHost));
    session.Followers->clear();
    session.Frame(3);
    CHECK(session.FollowerBindings.All().empty());
    CHECK(session.FollowerTargets.empty());
}

TEST_CASE("Followers: teammate recruitment retains its cause until vanilla fills the alias",
          "[HostAuthority][followers]")
{
    Session session(true);
    Ready(session);
    session.Followers->clear();
    session.FollowerOrders.push_back({kFollower, session.CharacterId(kPeer), Followers::Command::Recruit});
    session.Frame(1);
    CHECK(session.FollowerBindings.All().empty());
    session.Followers = std::vector<Followers::State>{{kFollower, {0, 0x31D2E, {}}, false, false}};
    session.Frame(2);
    CHECK(Target(session) == session.CharacterId(kPeer));
    session.Followers.reset();
    session.Frame(3);
    CHECK(session.FollowerBindings.Desired(kFollower) == "guest");
}

#include <Messages/FollowerMessages.h>

TEST_CASE("Followers: trade goes only to its picker and the host serializes access to that NPC",
          "[HostAuthority][followers]")
{
    Session session(true);
    Ready(session);
    const uint32_t cNpc = session.Character(kHost);
    session.Followers->front().Session = cNpc;
    session.FollowerBindings.Follow(kFollower, "other");
    session.Outbox.clear();
    session.FollowerOrders = {{kFollower, session.CharacterId(kPeer), Followers::Command::Trade},
                              {kFollower, session.CharacterId(kHost), Followers::Command::Trade}};
    session.Frame(1);
    REQUIRE(session.SentTo<NotifyFollowerTrade>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyFollowerTrade>(kPeer)[0]->NpcId == cNpc);
    CHECK(session.SentTo<NotifyFollowerTrade>(kHost).empty());
    CHECK(session.SentTo<NotifyFollowerTrade>(kOtherPeer).empty());
    CHECK(session.FollowerBindings.Desired(kFollower) == "other");

    FollowerTradeClosedRequest close;
    close.NpcId = cNpc;
    session.Receive(kOtherPeer, close); // cannot release someone else's trade
    session.FollowerOrders.push_back({kFollower, session.CharacterId(kHost), Followers::Command::Trade});
    session.Frame(2);
    CHECK(session.SentTo<NotifyFollowerTrade>(kHost).empty());
    session.Receive(kPeer, close);
    session.FollowerOrders.push_back({kFollower, session.CharacterId(kHost), Followers::Command::Trade});
    session.Frame(3);
    CHECK(session.SentTo<NotifyFollowerTrade>(kHost).size() == 1);
}

TEST_CASE("Followers: the shared humanoid and animal slots keep independent anchors", "[HostAuthority][followers]")
{
    Session session(true);
    Ready(session);
    constexpr uint32_t cAnimal = 0x109487;
    session.Followers->push_back({cAnimal, {0, 0x31D2E, {}}, false, false});
    session.FollowerOrders = {{kFollower, session.CharacterId(kPeer), Followers::Command::Recruit},
                              {cAnimal, session.CharacterId(kOtherPeer), Followers::Command::Recruit}};
    session.Frame(1);
    REQUIRE(session.FollowerTargets.size() == 2);
    CHECK(session.FollowerTargets[0].Character == session.CharacterId(kPeer));
    CHECK(session.FollowerTargets[1].Character == session.CharacterId(kOtherPeer));
    session.Followers->front().Dead = true;
    session.Frame(2);
    REQUIRE(session.FollowerTargets.size() == 1);
    CHECK(session.FollowerTargets.front().Reference == cAnimal);
    CHECK(session.FollowerBindings.Desired(kFollower).empty());
    CHECK(session.FollowerBindings.Desired(cAnimal) == "other");
}
