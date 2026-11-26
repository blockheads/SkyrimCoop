#include <combat/HostCombat.h>

#include <characters/HostCharacters.h>
#include <combat/HostCombatGame.h>
#include <core/HostPlayers.h>
#include <core/HostRouter.h>
#include <Messages/Relayed.h>

#include <algorithm>
#include <array>
#include <cmath>

HostCombat::HostCombat(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostCombatGame& acGame) noexcept
    : m_router(aRouter)
    , m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
    aRouter.On(*this, &HostCombat::OnMeleeStagger);
    aRouter.On(*this, &HostCombat::OnTrapContact);
    aRouter.On(*this, &HostCombat::OnTrapHit);
}

void HostCombat::OnMeleeStagger(const MeleeStaggerRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    if (m_characters.OwnerOf(acRequest.HitterId) != aSender || !m_characters.IsPlayerCharacter(acRequest.HitterId) ||
        !std::isfinite(acRequest.Magnitude) || acRequest.Magnitude <= 0.f)
        return;
    NotifyMeleeStagger notify;
    static_cast<Payloads::MeleeStagger&>(notify) = acRequest;
    m_router.Deliver(notify, HostRouter::Delivery::ToOwner, acRequest.HitteeId, aSender);
}

void HostCombat::OnTrapContact(const TrapContactRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    const auto cFinite = [](const std::array<float, 3>& acVector) { return std::ranges::all_of(acVector, [](float a) { return std::isfinite(a); }); };
    if (aSender == m_players.Host() || m_characters.OwnerOf(acRequest.TargetId) != aSender || !m_characters.IsPlayerCharacter(acRequest.TargetId) ||
        acRequest.Type > Payloads::TrapContact::kHit || !cFinite(acRequest.Velocity) || !cFinite(acRequest.Position) || !m_game.TouchTrap)
        return;
    m_game.TouchTrap(acRequest);
}

void HostCombat::OnTrapHit(const TrapHitRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    // Only the host's game runs the traps' scripts.
    if (aSender != m_players.Host() || !m_characters.IsPlayerCharacter(acRequest.TargetId))
        return;
    NotifyTrapHit notify;
    static_cast<Payloads::TrapHit&>(notify) = acRequest;
    m_router.Deliver(notify, HostRouter::Delivery::ToOwner, acRequest.TargetId, aSender);
}
