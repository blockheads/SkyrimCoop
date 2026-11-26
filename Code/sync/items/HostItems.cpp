#include <items/HostItems.h>
#include <characters/HostCharacters.h>
#include <items/HostPartyItems.h>
#include <Messages/ContainerTransferRequest.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>

#include <core/RecipeBook.h>
#include <core/HostRouter.h>
#include <Messages/Relayed.h>
#include <items/HostItemsGame.h>
#include <core/HostNetwork.h>
#include <core/HostPlayers.h>

#include <algorithm>

HostItems::HostItems(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharacters& acCharacters, HostPartyItems& aPartyItems, const HostItemsGame& acGame) noexcept
    : m_router(aRouter)
    , m_network(aNetwork)
    , m_players(acPlayers)
    , m_characters(acCharacters)
    , m_partyItems(aPartyItems)
    , m_game(acGame)
{
    aRouter.On(*this, &HostItems::OnContainerTransfer);
    aRouter.On(*this, &HostItems::OnDrop);
    aRouter.On(*this, &HostItems::OnPickUp);
    aRouter.On(*this, &HostItems::OnInventoryChanges);
    aRouter.On(*this, &HostItems::OnRecipes);
}

void HostItems::OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) noexcept
{
    // An actor's items reach its copies as its owner's part (HostRecords, kItems); the party's gold and items here.
    if (m_partyItems.Change(acRequest, aSender))
        return;
    // The host's script changed another game's actor (a quest taking a guest's claw): its owner runs it, and its part follows.
    if (aSender == m_players.Host() && acRequest.UpdateClients && m_characters.OwnerOf(acRequest.SessionId) != aSender)
    {
        NotifyInventoryChanges notify;
        notify.SessionId = acRequest.SessionId;
        notify.Item = acRequest.Item;
        m_router.Deliver(notify, HostRouter::Delivery::ToOwner, acRequest.SessionId, aSender);
    }
}

void HostItems::OnRecipes(const RecipesRequest& acRequest, ConnectionId_t) const noexcept
{
    if (RecipeBook* pBook = m_network.GetRecipes())
        for (const Recipe& cRecipe : acRequest.Known)
            pBook->Learn(cRecipe);
}

void HostItems::Update() noexcept
{
    std::erase_if(m_pending, [this](const Pending& acPending) { return Make(acPending.Player, acPending.Request); });
}

void HostItems::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    std::erase_if(m_pending, [aPlayer](const Pending& acPending) { return acPending.Player == aPlayer; });
    std::erase_if(m_made, [aPlayer](const auto& acMade) { return acMade.first.first == aPlayer; });
}

void HostItems::OnDrop(const DropItemRequest& acRequest, ConnectionId_t aSender) noexcept
{
    // The host's own drops are its world already (HostWorldState sends them).
    if (aSender == m_players.Host() || !m_game.CreateItem || Make(aSender, acRequest))
        return;
    spdlog::info("HostItems: {:x}'s drop {:X} ({:X}) is in a cell not loaded here, made once it is", aSender, acRequest.Tag,
                 acRequest.Item.BaseId.LogFormat());
    if (m_pending.size() >= kMaxPending)
    {
        spdlog::warn("HostItems: {} drops waiting for the host's game, forgetting {:X}", m_pending.size(), m_pending.front().Request.Tag);
        m_pending.erase(m_pending.begin());
    }
    m_pending.push_back({aSender, acRequest});
}

bool HostItems::Make(ConnectionId_t aPlayer, const DropItemRequest& acRequest) noexcept
{
    // A party item: the host's player dropped the real one for it already (HostPartyItems, from its item change).
    const std::optional<GameId> cDropped = m_partyItems.TakeDropped(aPlayer, acRequest.Item.BaseId);
    if (cDropped && *cDropped == GameId{})
    {
        spdlog::info("HostItems: {:x}'s drop {:X} of the party's {:X} refused: it goes back", aPlayer, acRequest.Tag, acRequest.Item.BaseId.LogFormat());
        Grant(aPlayer, {}, acRequest.Tag, acRequest.Item);
        return true;
    }
    const auto cObject = cDropped ? cDropped : m_game.CreateItem(acRequest.Item, acRequest.Placement);
    if (!cObject)
        return false;
    if (*cObject == GameId{})
        spdlog::warn("HostItems: {:x}'s drop {:X} ({:X}) cannot be made in the host's game, left its own", aPlayer, acRequest.Tag,
                     acRequest.Item.BaseId.LogFormat());
    else
    {
        m_made[{aPlayer, acRequest.Tag}] = *cObject;
        spdlog::info("HostItems: made {:x}'s drop {:X} as {:X} ({} of {:X})", aPlayer, acRequest.Tag, cObject->LogFormat(),
                     acRequest.Item.Count, acRequest.Item.BaseId.LogFormat());
    }
    // Before the creation goes out (HostWorldState, next update): the dropper's own reference becomes its twin (an empty
    // id: stays its own).
    NotifyItemDropped made;
    made.Tag = acRequest.Tag;
    made.Object = *cObject;
    m_network.Send(aPlayer, made);
    return true;
}

void HostItems::OnPickUp(const PickUpRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (aSender == m_players.Host() || !m_game.TakeItem)
        return;
    // Its own drop still waiting for its cell: never made, it goes back.
    const auto cPending = std::ranges::find_if(m_pending, [&](const Pending& acPending) {
        return acRequest.Tag && acPending.Player == aSender && acPending.Request.Tag == acRequest.Tag;
    });
    if (cPending != m_pending.end())
    {
        const Inventory::Entry cItem = cPending->Request.Item;
        m_pending.erase(cPending);
        Grant(aSender, {}, acRequest.Tag, cItem);
        return;
    }
    // Its own drop made here, asked for by its tag before the answer reached it.
    GameId object = acRequest.Object;
    if (const auto cMade = m_made.find({aSender, acRequest.Tag}); acRequest.Tag && cMade != m_made.end())
        object = cMade->second;
    std::erase_if(m_made, [&](const auto& acMade) { return acMade.second == object; });
    // An item a quest alias holds is the party's: held by the host's player, so the host's quests see the player hold it.
    if (object != GameId{} && m_game.TakeQuestObject && m_game.TakeQuestObject(object))
    {
        spdlog::info("HostItems: {:x} picked up {:X}, an alias holds it: the party holds it", aSender, object.LogFormat());
        return;
    }
    if (const auto cItem = object != GameId{} ? m_game.TakeItem(object) : std::nullopt)
        Grant(aSender, object, acRequest.Tag, *cItem);
    else
        spdlog::info("HostItems: {:x} asked for {:X} (tag {:X}), already gone", aSender, object.LogFormat(), acRequest.Tag);
}

void HostItems::Grant(ConnectionId_t aPlayer, const GameId& acObject, uint32_t aTag, const Inventory::Entry& acItem) const noexcept
{
    spdlog::info("HostItems: {:x} picks up {:X} (tag {:X}): {} of {:X}", aPlayer, acObject.LogFormat(), aTag, acItem.Count, acItem.BaseId.LogFormat());
    NotifyPickUp granted;
    granted.Object = acObject;
    granted.Tag = aTag;
    granted.Item = acItem;
    m_network.Send(aPlayer, granted);
}

void HostItems::OnContainerTransfer(const ContainerTransferRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const uint32_t cPlayer = m_characters.PlayerCharacterOf(aSender);
    if (aSender == m_players.Host() || !cPlayer || !m_game.TransferContainer || !acRequest.Item.Count ||
        acRequest.Item.Count == INT32_MIN || (acRequest.ContainerActorId &&
        (m_characters.IsPlayerCharacter(acRequest.ContainerActorId) || m_characters.OwnerOf(acRequest.ContainerActorId) != m_players.Host())))
        return;
    // Before the move: the host's player's arrival of a quest item is told in chat as this guest's find.
    const GameId cItem = acRequest.Item.BaseId;
    if (acRequest.Item.Count > 0)
        m_partyItems.FoundBy(cItem, aSender);
    Inventory::Entry moved = m_game.TransferContainer(acRequest, cPlayer);
    if (acRequest.Item.Count > 0 && !(moved.Count > 0 && moved.IsQuestItem))
        m_partyItems.FoundBy(cItem, 0);
    if (!moved.Count)
        return;
    // A quest alias's item went to the host's player with its alias (HostEngine::MoveAliasStack): its arrival there
    // makes it the party's, as a loose one's pickup does.
    if (moved.Count > 0 && moved.IsQuestItem)
    {
        spdlog::info("HostItems: {:x} took quest item {:X}: the party holds it", aSender, moved.BaseId.LogFormat());
        return;
    }
    // A put the host's player made with its real stack: its change, so the party's count is not taken twice.
    const bool cHostsOwn = moved.IsQuestItem;
    moved.IsQuestItem = false;
    RequestInventoryChanges change;
    change.SessionId = cHostsOwn ? m_characters.PlayerCharacterOf(m_players.Host()) : cPlayer;
    change.Item = moved;
    // Gold and party stacks are the party's (party-purse.md, party-items.md). The rest: the looter's own game changes its
    // player, whose items part goes to the others; the NPC's goes out as the host's own part (ItemsChangedEvent).
    // Gold: as the host's change, so the taker's HUD shows it too.
    const bool cGold = moved.BaseId == Payloads::PartyGold::kGold;
    if (!m_partyItems.Change(change, cGold || cHostsOwn ? m_players.Host() : aSender, false))
    {
        NotifyInventoryChanges notify;
        notify.SessionId = cPlayer;
        notify.Item = moved;
        m_network.Send(aSender, notify);
    }
}
