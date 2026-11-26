#include <magic/HostMagic.h>

#include <characters/HostCharacters.h>
#include <magic/HostMagicGame.h>
#include <core/HostPlayers.h>
#include <core/HostRouter.h>
#include <Messages/Relayed.h>

#include <Messages/SummonRequest.h>

HostMagic::HostMagic(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostMagicGame& acGame) noexcept
    : m_router(aRouter)
    , m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
    using enum HostRouter::Delivery;
    // An effect landing works like a hit.
    aRouter.Relay<InterruptCastRequest, NotifyInterruptCast>(FromOwner, &InterruptCastRequest::CasterId);
    aRouter.Relay<AddTargetRequest, NotifyAddTarget>(ToOwner, &AddTargetRequest::TargetId);
    aRouter.Relay<RemoveSpellRequest, NotifyRemoveSpell>(FromAnyone);
    // What shows on an actor (burning, frost): its owner's active effects, played as visuals only.
    aRouter.Relay<EffectFlashRequest, NotifyEffectFlash>(FromOwner, &EffectFlashRequest::SessionId);
    aRouter.On(*this, &HostMagic::OnProjectileLaunch);
    aRouter.On(*this, &HostMagic::OnSummon);
}

void HostMagic::OnSummon(const SummonRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    const uint32_t cCaster = aSender != m_players.Host() ? m_characters.PlayerCharacterOf(aSender) : 0;
    if (!cCaster || !m_game.Summon)
        return;
    m_game.Summon(cCaster, acRequest);
}

void HostMagic::OnProjectileLaunch(const ProjectileLaunchRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyProjectileLaunch notify;
    static_cast<Payloads::ProjectileLaunch&>(notify) = acRequest;
    m_router.Deliver(notify, acRequest.ShooterID ? HostRouter::Delivery::FromOwner : HostRouter::Delivery::FromAnyone, acRequest.ShooterID, aSender);
}
