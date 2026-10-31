#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostPurse.h>

#include <Services/Host/HostAuthority.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/Relayed.h>
#include <Messages/RequestInventoryChanges.h>

#include <random>

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
    // The engine shows its own player's change; a host script's on a guest's character no game showed.
    const ConnectionId_t cChanger = m_characters.OwnerOf(acRequest.SessionId);
    Send(acRequest.Item.Count, cChanger == aSender ? cChanger : 0);
    if (acRequest.Item.Count < 0)
    {
        // Every game's chat tells who spent the party's gold.
        static std::mt19937 s_random{std::random_device{}()};
        NotifyChatMessageBroadcast line;
        line.MessageType = kSystemMessage;
        line.ChatMessage = ChatText(std::format("{} {} {} gold", m_players.PlayerNameOf(cChanger),
                                                cSpendWords[s_random() % cSpendWords.size()], -acRequest.Item.Count));
        m_players.SendToAll(line);
    }
    return true;
}

void HostPurse::Send(int32_t aShown, ConnectionId_t aChanger) const noexcept
{
    if (!m_game.ReadPartyGold)
        return;
    NotifyPartyGold notify;
    notify.Gold = m_game.ReadPartyGold();
    if (aChanger)
        m_players.SendTo(aChanger, notify);
    notify.Shown = aShown;
    m_players.SendToOthers(notify, aChanger);
}
