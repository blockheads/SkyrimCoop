#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPartyItems.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostAuthority.h>

#include <Games/PartyItems.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/Relayed.h>
#include <Messages/RequestInventoryChanges.h>

#include <random>

HostPartyItems::HostPartyItems(const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers), m_characters(acCharacters), m_game(acGame)
{
}

void HostPartyItems::Send() const noexcept
{
    if (m_game.ReadPartyGold)
    {
        NotifyPartyGold gold;
        gold.Gold = m_game.ReadPartyGold();
        m_players.SendToAll(gold);
    }
    if (!m_game.ReadPartyItems)
        return;
    NotifyPartyItems items;
    items.Items = m_game.ReadPartyItems();
    for (Inventory::Entry& item : items.Items)
        item.ExtraWorn = item.ExtraWornLeft = false;
    m_players.SendToAll(items);
}

template <class T> void HostPartyItems::SendChange(T aNotify, int32_t aChange, ConnectionId_t aShownThere) const noexcept
{
    if (aShownThere)
        m_players.SendTo(aShownThere, aNotify);
    aNotify.Shown = aChange;
    m_players.SendToOthers(aNotify, aShownThere);
}

bool HostPartyItems::Change(const RequestInventoryChanges& acFound, ConnectionId_t aSender, bool aInFinderPack) noexcept
{
    // A guest's plain arrival: the party's when the rule says so (its finder renames its own, Converted below).
    RequestInventoryChanges acRequest = acFound;
    int32_t converted = 0;
    const bool cGuestArrival = aSender != m_players.Host() && acFound.Item.Count > 0 && !acFound.Drop &&
                               !acFound.Item.ExtraName.starts_with(PartyItems::cTag) && m_characters.OwnerOf(acFound.SessionId) == aSender;
    if (cGuestArrival && !acFound.NewPartyItem && m_game.ItemFormType &&
        PartyItems::ArrivesAsParty(acFound.Item.BaseId.BaseId, m_game.ItemFormType(acFound.Item.BaseId), acFound.Item.IsQuestItem,
                                   acFound.StoryGiven, acFound.AliasHeld) &&
        acFound.Item.BaseId != Payloads::PartyGold::kGold)
    {
        acRequest.NewPartyItem = true;
        if (!acFound.Item.IsQuestItem && m_game.ItemName)
        {
            acRequest.Item.ExtraName = PartyItems::Tagged(acFound.Item.ExtraName, m_game.ItemName(acFound.Item.BaseId));
            acRequest.Item.ExtraWorn = acRequest.Item.ExtraWornLeft = false;
            converted = aInFinderPack ? acFound.Item.Count : 0;
        }
    }
    const bool cGold = acRequest.Item.BaseId == Payloads::PartyGold::kGold;
    if ((!cGold && (!PartyItems::IsParty(acRequest.Item) || !m_game.ReadPartyItem)) || !m_characters.IsPlayerCharacter(acRequest.SessionId))
        return false;
    // The refused drop's item coming back to its dropper (HostItems::Make): nothing changed.
    const ConnectionId_t cHost = m_players.Host();
    if (const auto it = m_refused.find({aSender, acRequest.Item.BaseId.LogFormat()});
        it != m_refused.end() && aSender != cHost && acRequest.Item.Count > 0 && !acRequest.Drop)
    {
        if ((it->second -= acRequest.Item.Count) <= 0)
            m_refused.erase(it);
        return true;
    }
    // A guest changes only its own player's; the host's scripts any player's (rule 2: a guest's copy).
    const ConnectionId_t cChanger = m_characters.OwnerOf(acRequest.SessionId);
    if (aSender != cHost && cChanger != aSender)
        return true;
    // The host's player's own change is in its inventory already (an arrival renamed where it is).
    const bool cApply = acRequest.SessionId != m_characters.PlayerCharacterOf(cHost);
    // The engine shows its own player's change; a host script's on a guest's character no game showed.
    const ConnectionId_t cShownThere = cChanger == aSender ? cChanger : 0;
    if (cGold)
    {
        if (cApply && m_game.AddPartyGold)
            m_game.AddPartyGold(acRequest.Item.Count);
        if (m_game.ReadPartyGold)
        {
            NotifyPartyGold notify;
            notify.Gold = m_game.ReadPartyGold();
            SendChange(notify, acRequest.Item.Count, cShownThere);
        }
        if (acRequest.Item.Count < 0)
            TellSpent(cChanger, -acRequest.Item.Count);
        return true;
    }
    if (cApply)
    {
        // A guest's drop of its mirror: the host's player drops the real one where it stands, alias and all.
        std::optional<GameId> dropped;
        if (acRequest.Drop && acRequest.Item.Count < 0 && aSender != cHost && m_game.DropPartyItem)
            dropped = m_game.DropPartyItem(acRequest.Item, acRequest.SessionId);
        if (dropped)
            m_dropped[{aSender, acRequest.Item.BaseId.LogFormat()}] = *dropped;
        if (dropped && *dropped == GameId{})
        {
            // Refused: the dropper gets its drop back (HostItems::Make), the party's count unchanged.
            m_refused[{aSender, acRequest.Item.BaseId.LogFormat()}] -= acRequest.Item.Count;
            return true;
        }
        if (!dropped)
            m_game.AddPartyItem(acRequest.Item);
    }
    NotifyPartyItem notify;
    notify.Item = acRequest.Item;
    // A quest item keeps its alias's name on its holder; every other player's copy is named with the tag.
    if (notify.Item.IsQuestItem && notify.Item.ExtraName.empty() && m_game.ItemName)
        notify.Item.ExtraName = PartyItems::Tagged("", m_game.ItemName(notify.Item.BaseId));
    notify.Item.Count = m_game.ReadPartyItem(notify.Item);
    notify.Item.ExtraWorn = notify.Item.ExtraWornLeft = false;
    if (cShownThere)
    {
        notify.Converted = converted;
        m_players.SendTo(cShownThere, notify);
        notify.Converted = 0;
    }
    notify.Shown = acRequest.Item.Count;
    m_players.SendToOthers(notify, cShownThere);
    if (acRequest.NewPartyItem && acRequest.Item.Count > 0)
    {
        ConnectionId_t finder = cChanger;
        if (const auto it = m_finders.find(acRequest.Item.BaseId); it != m_finders.end())
        {
            if (aSender == cHost)
                finder = it->second;
            m_finders.erase(it);
        }
        TellFound(acRequest, finder);
    }
    return true;
}

void HostPartyItems::TellSpent(ConnectionId_t aPlayer, int32_t aGold) const noexcept
{
    static std::mt19937 s_random{std::random_device{}()};
    NotifyChatMessageBroadcast line;
    line.MessageType = kSystemMessage;
    line.ChatMessage = ChatText(std::format("{} {} {} gold", m_players.PlayerNameOf(aPlayer), cSpendWords[s_random() % cSpendWords.size()], aGold));
    m_players.SendToAll(line);
}

// "Lydia found Lucan's Claw for the party".
void HostPartyItems::TellFound(const RequestInventoryChanges& acRequest, ConnectionId_t aPlayer) const noexcept
{
    const std::string cName = acRequest.Item.ExtraName.starts_with(PartyItems::cTag)
                                  ? acRequest.Item.ExtraName.substr(PartyItems::cTag.size())
                                  : (m_game.ItemName ? m_game.ItemName(acRequest.Item.BaseId) : std::string());
    NotifyChatMessageBroadcast line;
    line.MessageType = kSystemMessage;
    line.ChatMessage = ChatText(acRequest.StoryGiven ? std::format("The party received {}", cName)
                                                     : std::format("{} found {} for the party", m_players.PlayerNameOf(aPlayer), cName));
    m_players.SendToAll(line);
}
