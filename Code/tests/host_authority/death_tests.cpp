#include "session.h"
#include <Messages/DeathMessages.h>
#include <Messages/Relayed.h>
#include <Messages/RelayedMessages.h>

using namespace HostAuthorityTest;

namespace
{
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

struct Party
{
    Party()
    {
        Body(Game, kHost);
        Body(Game, kPeer);
        Body(Game, kOtherPeer);
    }
    void Falls(ConnectionId_t aPlayer, uint32_t aGeneration, bool aDown = true)
    {
        PlayerDeathRequest down;
        down.SessionId = Game.CharacterId(aPlayer);
        down.Generation = aGeneration;
        down.Down = aDown;
        Game.Receive(aPlayer, down);
    }
    void Fights(ConnectionId_t aPlayer, bool aFights = true)
    {
        AiStateRequest fight;
        fight.SessionId = Game.CharacterId(aPlayer);
        fight.State.Combat = aFights ? 1 : 0;
        Game.Receive(aPlayer, fight);
    }
    void At(double aSeconds)
    {
        Game.Frame(static_cast<uint64_t>(aSeconds * 1000.0));
    }
    const NotifyPartyDeath& Latest() const
    {
        const auto cAll = Game.SentTo<NotifyPartyDeath>(kHost);
        REQUIRE(!cAll.empty());
        return *cAll.back();
    }
    Session Game;
};
} // namespace

TEST_CASE("With no fight a downed player's recovery starts at once", "[HostAuthority][Death]")
{
    Party party;
    party.Falls(kHost, 1);
    CHECK_FALSE(party.Latest().Combat);
    CHECK(party.Latest().RemainingMs == 10000);
}

TEST_CASE("A fight holds a downed player's recovery until the hold's end, then the recovery runs", "[HostAuthority][Death]")
{
    Party party;
    party.Fights(kPeer);
    party.Falls(kHost, 1);
    CHECK(party.Latest().Combat);
    CHECK(party.Latest().RemainingMs == 0);

    party.At(HostDeath::kCombatHoldSeconds - 1.0);
    CHECK(party.Latest().Combat);
    party.At(HostDeath::kCombatHoldSeconds);
    CHECK_FALSE(party.Latest().Combat);
    CHECK(party.Latest().RemainingMs == 10000);
}

TEST_CASE("A later fall gets a hold of its own", "[HostAuthority][Death]")
{
    Party party;
    party.Fights(kPeer);
    party.Falls(kHost, 1);
    party.At(HostDeath::kCombatHoldSeconds);
    party.At(HostDeath::kCombatHoldSeconds + HostDeath::kRecoverySeconds);
    const auto cRespawns = party.Game.SentTo<NotifyPlayerRespawn>(kHost);
    REQUIRE(cRespawns.size() == 1);
    party.Falls(kHost, 1, false);
    CHECK(party.Latest().Combat);

    party.Falls(kHost, 2);
    party.At(HostDeath::kCombatHoldSeconds + HostDeath::kRecoverySeconds + 1.0);
    CHECK(party.Latest().Combat);
}
