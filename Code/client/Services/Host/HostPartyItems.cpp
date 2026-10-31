#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPartyItems.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostAuthority.h>

#include <Games/PartyItems.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/Relayed.h>
#include <Messages/RequestInventoryChanges.h>

HostPartyItems::HostPartyItems(const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers), m_characters(acCharacters), m_game(acGame)
{
}

void HostPartyItems::Send() const noexcept
{
    if (!m_game.ReadPartyItems)
        return;
    NotifyPartyItems notify;
    notify.Items = m_game.ReadPartyItems();
    for (Inventory::Entry& item : notify.Items)
        item.ExtraWorn = item.ExtraWornLeft = false;
    m_players.SendToAll(notify);
}

bool HostPartyItems::Change(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    if (!PartyItems::IsParty(acRequest.Item) || !m_characters.IsPlayerCharacter(acRequest.SessionId) || !m_game.ReadPartyItem)
        return false;
    // A guest changes only its own player's; the host's scripts any player's (rule 2: a guest's copy).
    const ConnectionId_t cHost = m_players.Host();
    const ConnectionId_t cChanger = m_characters.OwnerOf(acRequest.SessionId);
    if (aSender != cHost && cChanger != aSender)
        return true;
    // The host's player's own change of a tagged stack is in its inventory already; an arrival its game took back is not.
    if (acRequest.SessionId != m_characters.PlayerCharacterOf(cHost) || (acRequest.NewPartyItem && !acRequest.Item.IsQuestItem))
        m_game.AddPartyItem(acRequest.Item);

    NotifyPartyItem notify;
    notify.Item = acRequest.Item;
    // A quest item keeps its alias's name on its holder; every other player's copy is named with the tag.
    if (notify.Item.IsQuestItem && notify.Item.ExtraName.empty() && m_game.ItemName)
        notify.Item.ExtraName = PartyItems::Tagged("", m_game.ItemName(notify.Item.BaseId));
    notify.Item.Count = m_game.ReadPartyItem(notify.Item);
    notify.Item.ExtraWorn = notify.Item.ExtraWornLeft = false;
    // The engine shows its own player's change; a host script's on a guest's character no game showed.
    const ConnectionId_t cShownThere = cChanger == aSender ? cChanger : 0;
    if (cShownThere)
        m_players.SendTo(cShownThere, notify);
    notify.Shown = acRequest.Item.Count;
    m_players.SendToOthers(notify, cShownThere);

    if (acRequest.NewPartyItem && acRequest.Item.Count > 0)
    {
        // Every game's chat tells what the party got ("Lydia found Lucan's Claw for the party").
        const std::string cName = acRequest.Item.ExtraName.starts_with(PartyItems::cTag)
                                      ? acRequest.Item.ExtraName.substr(PartyItems::cTag.size())
                                      : (m_game.ItemName ? m_game.ItemName(acRequest.Item.BaseId) : std::string());
        NotifyChatMessageBroadcast line;
        line.MessageType = kSystemMessage;
        line.ChatMessage = ChatText(acRequest.StoryGiven ? std::format("The party received {}", cName)
                                                         : std::format("{} found {} for the party", m_players.PlayerNameOf(cChanger), cName));
        m_players.SendToAll(line);
    }
    return true;
}
