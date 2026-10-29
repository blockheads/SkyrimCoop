// A guest's story events (party-model.md rule 4): raised in the host's game for the guest's character.
#include "session.h"

#include <Messages/StoryEventRequest.h>

namespace
{
// Every player's character announced, the host's own too; its session id by owner.
std::map<ConnectionId_t, uint32_t> Players(Session& aSession)
{
    std::map<ConnectionId_t, uint32_t> characters;
    for (const ConnectionId_t cPlayer : {kHost, kPeer, kOtherPeer})
    {
        AnnounceCharacterRequest announce;
        announce.SessionId = aSession.CharacterId(cPlayer);
        announce.ReferenceId = GameId(0, 0x14);
        aSession.Receive(cPlayer, announce);
        characters[cPlayer] = announce.SessionId;
    }
    aSession.Outbox.clear();
    return characters;
}

StoryEventRequest Craft()
{
    StoryEventRequest request;
    request.Event.Type = StoryEvent::kCraft;
    request.Event.Workbench = GameId(0, 0x1A2B3);
    request.Event.Location = GameId(0, 0x18A56);
    request.Event.Created = GameId(0, 0x1397E);
    return request;
}
} // namespace

TEST_CASE("A guest's craft is raised in the host's game for its character", "[HostAuthority][Story]")
{
    Session session;
    const auto cCharacters = Players(session);

    session.Receive(kPeer, Craft());
    REQUIRE(session.StoriesRaised.size() == 1);
    const StoryEvent& cRaised = session.StoriesRaised[0];
    CHECK(cRaised.Type == StoryEvent::kCraft);
    CHECK(cRaised.Character == cCharacters.at(kPeer));
    CHECK(cRaised.Workbench == GameId(0, 0x1A2B3));
    CHECK(cRaised.Location == GameId(0, 0x18A56));
    CHECK(cRaised.Created == GameId(0, 0x1397E));
    // Nothing goes to the other games: the host's quests reach them as world state.
    CHECK(session.Outbox.empty());

    // The host's own craft its engine raised already.
    session.Receive(kHost, Craft());
    CHECK(session.StoriesRaised.size() == 1);

    // A guest names only its own events: an arrival is the host's game's to see.
    StoryEventRequest arrival;
    arrival.Event.Type = StoryEvent::kLocationChange;
    arrival.Event.Location = GameId(0, 0x18A56);
    session.Receive(kOtherPeer, arrival);
    CHECK(session.StoriesRaised.size() == 1);
}

TEST_CASE("A guest's character arriving somewhere on the host is raised for it", "[HostAuthority][Story]")
{
    Session session;
    const auto cCharacters = Players(session);
    const uint32_t cNpc = session.Character(kHost);

    session.CharacterMoved(cCharacters.at(kPeer), GameId(0, 0x1), GameId(0, 0x18A56));
    REQUIRE(session.StoriesRaised.size() == 1);
    CHECK(session.StoriesRaised[0].Type == StoryEvent::kLocationChange);
    CHECK(session.StoriesRaised[0].Character == cCharacters.at(kPeer));
    CHECK(session.StoriesRaised[0].OldLocation == GameId(0, 0x1));
    CHECK(session.StoriesRaised[0].Location == GameId(0, 0x18A56));

    // The host's player's arrival its engine raises; an NPC's is nobody's story.
    session.CharacterMoved(cCharacters.at(kHost), GameId(0, 0x1), GameId(0, 0x18A56));
    session.CharacterMoved(cNpc, GameId(0, 0x1), GameId(0, 0x18A56));
    CHECK(session.StoriesRaised.size() == 1);
}
