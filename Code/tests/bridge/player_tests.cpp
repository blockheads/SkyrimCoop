// The session's players: their character levels, for the player list.
#include "session.h"

namespace
{
PlayerLevelRequest LevelUp(uint16_t aLevel)
{
    PlayerLevelRequest request;
    request.NewLevel = aLevel;
    return request;
}
} // namespace

TEST_CASE("Player level: a level-up reaches everyone else under the player's id", "[Bridge][Player]")
{
    Session session;
    const uint32_t cPeerId = session.World().GetPlayerManager().GetByConnectionId(kPeer)->GetId();

    session.Receive(kPeer, LevelUp(12));
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifyPlayerLevel>(cTo);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->PlayerId == cPeerId);
        CHECK(cNotifies[0]->NewLevel == 12);
    }
    CHECK(session.SentTo<NotifyPlayerLevel>(kPeer).empty());
}

TEST_CASE("Player level: a player joining later sees each player's level as it is now", "[Bridge][Player]")
{
    Session session;
    const uint32_t cPeerId = session.World().GetPlayerManager().GetByConnectionId(kPeer)->GetId();
    session.Receive(kPeer, LevelUp(12));
    session.Outbox.clear();

    constexpr ConnectionId_t cLateJoiner = 4;
    AuthenticationRequest join;
    join.Version = BuildCommit();
    join.Username = "Serana";
    join.Level = 30;
    session.Receive(cLateJoiner, join);

    const auto cPlayers = session.SentTo<NotifyPlayerJoined>(cLateJoiner);
    const auto cPeer = std::find_if(cPlayers.begin(), cPlayers.end(), [cPeerId](const auto* apJoined) { return apJoined->PlayerId == cPeerId; });
    REQUIRE(cPeer != cPlayers.end());
    CHECK((*cPeer)->Level == 12);

    // And everyone else learns the newcomer's.
    const auto cAnnounced = session.SentTo<NotifyPlayerJoined>(kPeer);
    REQUIRE(cAnnounced.size() == 1);
    CHECK(cAnnounced[0]->Username == "Serana");
    CHECK(cAnnounced[0]->Level == 30);
}

TEST_CASE("Player level: level 0 is refused", "[Bridge][Player]")
{
    Session session;
    session.Receive(kPeer, LevelUp(0));
    CHECK(session.Outbox.empty());
}
