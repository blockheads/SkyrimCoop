#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostPurse.h>

#include <Messages/Relayed.h>
#include <Messages/RequestInventoryChanges.h>

HostPurse::HostPurse(const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers), m_characters(acCharacters), m_game(acGame)
{
}

bool HostPurse::Change(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    if (acRequest.Item.BaseId != Payloads::PartyGold::kGold || !m_characters.IsPlayerCharacter(acRequest.SessionId))
        return false;
    // A guest changes only its own player's; the host's scripts any player's (rule 2: a guest's copy).
    const ConnectionId_t cHost = m_players.Host();
    if (aSender != cHost && m_characters.OwnerOf(acRequest.SessionId) != aSender)
        return true;
    // The host's player's own change is in its gold already.
    if (acRequest.SessionId != m_characters.PlayerCharacterOf(cHost) && m_game.AddPartyGold)
        m_game.AddPartyGold(acRequest.Item.Count);
    Send();
    return true;
}

void HostPurse::Send() const noexcept
{
    if (!m_game.ReadPartyGold)
        return;
    NotifyPartyGold notify;
    notify.Gold = m_game.ReadPartyGold();
    m_players.SendToAll(notify);
}
