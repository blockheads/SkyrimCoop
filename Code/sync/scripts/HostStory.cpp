#include <scripts/HostStory.h>

#include <core/HostRouter.h>
#include <characters/HostCharacters.h>
#include <scripts/HostStoryGame.h>
#include <core/HostPlayers.h>

#include <Messages/StoryEventRequest.h>

HostStory::HostStory(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostStoryGame& acGame) noexcept
    : m_players(acPlayers), m_characters(acCharacters), m_game(acGame)
{
    aRouter.On(*this, &HostStory::OnStoryEvent);
}

void HostStory::OnStoryEvent(const StoryEventRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    // Only a guest's: the host's own game raised the host's player's already.
    if (aSender == m_players.Host() || !m_game.RaiseStoryEvent)
        return;
    const uint32_t cCharacter = m_characters.PlayerCharacterOf(aSender);
    if (!cCharacter)
        return;
    StoryEvent event = acRequest.Event;
    event.Character = cCharacter;
    m_game.RaiseStoryEvent(event);
}
