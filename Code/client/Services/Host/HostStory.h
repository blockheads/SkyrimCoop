#pragma once

#include <network/ENetInterface.h>

#include <Structs/GameId.h>
#include <NoCopyMove.h>

struct HostAuthority;
struct HostCharacters;
struct HostGame;
struct HostPlayers;
struct StoryEventRequest;

/**
 * @brief A guest's story events (party-model.md rule 4): the host's story manager starts the party's quests, so a
 * guest's are raised in the host's game, judged for the guest's character there.
 */
struct HostStory
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostStory(HostAuthority& aAuthority, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostStory);

    // The host's game shows a character change location: a guest's is raised for it (the host's player's the engine
    // raises itself).
    void CharacterMoved(uint32_t aSessionId, const GameId& acOldLocation, const GameId& acLocation) const noexcept;

private:
    // A guest's own story event its game cannot see judged (a craft in its menu): raised for its character.
    void OnStoryEvent(const StoryEventRequest& acRequest, ConnectionId_t aSender) const noexcept;

    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
};
