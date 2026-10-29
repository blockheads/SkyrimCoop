#include <Services/Host/HostStory.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/StoryEventRequest.h>

HostStory::HostStory(HostAuthority& aAuthority, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers), m_characters(acCharacters), m_game(acGame)
{
    aAuthority.On(*this, &HostStory::OnStoryEvent);
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
