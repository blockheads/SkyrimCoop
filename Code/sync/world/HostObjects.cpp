#include <world/HostObjects.h>

#include <characters/HostCharacters.h>
#include <world/HostWorldGame.h>
#include <core/HostPlayers.h>
#include <core/HostRouter.h>

#include <Messages/ActivateRequest.h>
#include <Messages/ContainerChangeRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/ObjectStateRequest.h>
#include <world/DestructionRow.h>

HostObjects::HostObjects(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostWorldGame& acGame) noexcept
    : m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
    aRouter.On(*this, &HostObjects::OnActivate);
    aRouter.On(*this, &HostObjects::OnLockChange);
    aRouter.On(*this, &HostObjects::OnContainerChange);
    aRouter.On(*this, &HostObjects::OnObjectState);
}

void HostObjects::OnActivate(const ActivateRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    if (aSender == m_players.Host() || m_characters.OwnerOf(acRequest.ActivatorId) != aSender || !m_game.Activate)
        return;
    if (acRequest.NpcId && (m_characters.IsPlayerCharacter(acRequest.NpcId) || m_characters.OwnerOf(acRequest.NpcId) != m_players.Host()))
        return;
    m_game.Activate(acRequest.Id, acRequest.NpcId, acRequest.ActivatorId);
}

void HostObjects::OnLockChange(const LockChangeRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    if (aSender != m_players.Host() && m_game.SetLock)
        m_game.SetLock(acRequest.Id, acRequest.IsLocked, acRequest.LockLevel);
}

void HostObjects::OnContainerChange(const ContainerChangeRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    if (aSender == m_players.Host() || !m_game.SetItems)
        return;
    ObjectChange items{ObjectChange::Kind::kItems, acRequest.Container};
    items.Item = acRequest.Item;
    items.Value = acRequest.Count;
    items.Stacks = acRequest.Stacks;
    m_game.SetItems(items);
}

void HostObjects::OnObjectState(const ObjectStateRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    const ObjectChange& cProposed = acRequest.Change;
    if (aSender == m_players.Host() || cProposed.Type != ObjectChange::Kind::kDestruction || !m_game.ReadObjectState || !m_game.SetObjectState)
        return;
    const auto cHere = m_game.ReadObjectState(cProposed.Object, cProposed.Type);
    if (cHere && DestructionRow::GuestDamages(cHere->Value, cProposed.Value))
        m_game.SetObjectState(cProposed);
}
