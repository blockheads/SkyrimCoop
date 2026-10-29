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

void HostStory::CharacterMoved(uint32_t aSessionId, const GameId& acOldLocation, const GameId& acLocation) const noexcept
{
    if (!m_game.RaiseStoryEvent || !m_characters.IsPlayerCharacter(aSessionId))
        return;
    if (m_characters.OwnerOf(aSessionId) == m_players.Host())
        return;
    StoryEvent event;
    event.Type = StoryEvent::kLocationChange;
    event.Character = aSessionId;
    event.OldLocation = acOldLocation;
    event.Location = acLocation;
    spdlog::info("HostStory: guest's character {:X} arrived at {:X}, raised for it", aSessionId, acLocation.LogFormat());
    m_game.RaiseStoryEvent(event);
}

void HostStory::OnStoryEvent(const StoryEventRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    // Only a guest's: the host's own game raised the host's player's already.
    if (aSender == m_players.Host() || !m_game.RaiseStoryEvent)
        return;
    const uint32_t cCharacter = m_characters.PlayerCharacterOf(aSender);
    if (!cCharacter || acRequest.Event.Type == StoryEvent::kLocationChange)
        return;
    StoryEvent event = acRequest.Event;
    event.Character = cCharacter;
    spdlog::info("HostStory: guest {}'s craft of {:X} raised for its character {:X}", aSender, event.Created.LogFormat(), cCharacter);
    m_game.RaiseStoryEvent(event);
}
