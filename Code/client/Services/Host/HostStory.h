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

private:
    // A guest's own story event (any type its game raised): raised for its character.
    void OnStoryEvent(const StoryEventRequest& acRequest, ConnectionId_t aSender) const noexcept;

    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
};
