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

// A craft (CRFT): the workbench, its location, the item made.
StoryEventRequest Craft()
{
    using Member = StoryEvent::Member;
    StoryEventRequest request;
    request.Event.Type = StoryEvent::TypeOf("CRFT");
    request.Event.Members = {{Member::kReference, GameId(0, 0x1A2B3)},
                             {Member::kForm, GameId(0, 0x18A56)},
                             {Member::kForm, GameId(0, 0x1397E)}};
    return request;
}
} // namespace

TEST_CASE("A guest's story event is raised in the host's game for its character", "[HostAuthority][Story]")
{
    Session session;
    const auto cCharacters = Players(session);

    session.Receive(kPeer, Craft());
    REQUIRE(session.StoriesRaised.size() == 1);
    const StoryEvent& cRaised = session.StoriesRaised[0];
    CHECK(cRaised.Type == Craft().Event.Type);
    CHECK(cRaised.Character == cCharacters.at(kPeer));
    CHECK(cRaised.Members == Craft().Event.Members);
    // Nothing goes to the other games: the host's quests reach them as world state.
    CHECK(session.Outbox.empty());

    // Any type the guest's game raised, its arrival too (CLOC: its character, from, to).
    StoryEventRequest arrival;
    arrival.Event.Type = StoryEvent::TypeOf("CLOC");
    arrival.Event.Members = {{StoryEvent::Member::kCharacter}, {}, {StoryEvent::Member::kForm, GameId(0, 0x18A56)}};
    session.Receive(kOtherPeer, arrival);
    REQUIRE(session.StoriesRaised.size() == 2);
    CHECK(session.StoriesRaised[1].Character == cCharacters.at(kOtherPeer));

    // The host's own events its engine raised already.
    session.Receive(kHost, Craft());
    CHECK(session.StoriesRaised.size() == 2);
}
