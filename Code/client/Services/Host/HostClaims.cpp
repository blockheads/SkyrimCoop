#include <Services/Host/HostClaims.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/Relayed.h>

HostClaims::HostClaims(HostAuthority& aAuthority, const HostPlayers& acPlayers, HostCharacters& aCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers)
    , m_characters(aCharacters)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostClaims::OnClaim);
    aAuthority.On(*this, &HostClaims::OnGiveBack);
}

HostClaims::ConnectionId_t HostClaims::HolderOf(uint32_t aActorId) const noexcept
{
    const auto cIn = m_inClaim.find(aActorId);
    return cIn == m_inClaim.end() ? 0 : m_claims.at(cIn->second).To;
}

void HostClaims::OnClaim(const ClaimRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const auto cDistinct = [&] {
        for (size_t i = 0; i < acRequest.Members.size(); ++i)
            for (size_t j = i + 1; j < acRequest.Members.size(); ++j)
                if (acRequest.Members[i].ActorId == acRequest.Members[j].ActorId)
                    return false;
        return true;
    };
    if (m_characters.OwnerOf(acRequest.ForId) != aSender || acRequest.Members.empty() || !cDistinct())
    {
        spdlog::warn("HostClaims: {:x} claimed for {:x}, which it cannot", aSender, acRequest.ForId);
        return;
    }
    const auto cRule = m_rules.find(acRequest.Kind);
    // The same claim again (a rider getting back on the horse it holds): held already.
    if (const auto cIn = m_inClaim.find(acRequest.Members[0].ActorId); cIn != m_inClaim.end())
    {
        const Held& held = m_claims.at(cIn->second);
        if (held.To == aSender && held.Claim.Kind == acRequest.Kind && held.Claim.ForId == acRequest.ForId && held.Claim.Members == acRequest.Members)
        {
            if (cRule != m_rules.end() && cRule->second.Granted)
                cRule->second.Granted(held.Claim, aSender, false);
            return;
        }
    }
    for (const auto& cMember : acRequest.Members)
    {
        if (!m_characters.OwnerOf(cMember.ActorId))
            return Refuse(acRequest, Payloads::Claim::kCannotTake);
        // One claim per actor: two riders, crossed kill moves, or one still on its way back.
        if (m_inClaim.contains(cMember.ActorId) || Returning(cMember.ActorId))
            return Refuse(acRequest, Payloads::Claim::kHeld);
    }
    for (const auto& cMember : acRequest.Members)
        if (cMember.Take && (m_characters.OwnerOf(cMember.ActorId) != m_players.Host() || m_characters.IsPlayerCharacter(cMember.ActorId) ||
                             (m_game.Claimable && !m_game.Claimable(cMember.ActorId))))
            return Refuse(acRequest, Payloads::Claim::kCannotTake);
    if (cRule != m_rules.end() && cRule->second.Allows && !cRule->second.Allows(acRequest))
        return Refuse(acRequest, Payloads::Claim::kCannotTake);
    Grant(acRequest, aSender);
}

void HostClaims::Refuse(const ClaimRequest& acRequest, uint8_t aReason) noexcept
{
    spdlog::info("HostClaims: claim for {:x} at {} refused ({})", acRequest.ForId, acRequest.Tick, aReason);
    NotifyClaimRefused refused;
    static_cast<Payloads::Claim&>(refused) = acRequest;
    refused.ClaimId = 0;
    refused.Reason = aReason;
    m_players.SendToAll(refused);
}

void HostClaims::Grant(Payloads::Claim aClaim, ConnectionId_t aTo) noexcept
{
    aClaim.ClaimId = ++m_lastClaimId;
    aClaim.Reason = Payloads::Claim::kNone;
    bool ownersChanged = false;
    for (const auto& cMember : aClaim.Members)
    {
        m_inClaim[cMember.ActorId] = aClaim.ClaimId;
        if (cMember.Take)
            ownersChanged |= m_characters.SetOwner(cMember.ActorId, aTo);
    }
    m_claims[aClaim.ClaimId] = {aClaim, aTo};
    spdlog::info("HostClaims: {:x} holds claim {} for {:x} ({} members)", aTo, aClaim.ClaimId, aClaim.ForId, aClaim.Members.size());
    NotifyClaim notify;
    static_cast<Payloads::Claim&>(notify) = aClaim;
    m_players.SendToAll(notify);
    if (const auto cRule = m_rules.find(aClaim.Kind); cRule != m_rules.end() && cRule->second.Granted)
        cRule->second.Granted(aClaim, aTo, ownersChanged);
}

bool HostClaims::PassOn(uint32_t aActorId, ConnectionId_t aTo, uint32_t aForId) noexcept
{
    const auto cIn = m_inClaim.find(aActorId);
    if (cIn == m_inClaim.end())
        return false;
    Payloads::Claim claim = m_claims.at(cIn->second).Claim;
    m_claims.erase(cIn->second);
    for (const auto& cMember : claim.Members)
        m_inClaim.erase(cMember.ActorId);
    claim.ForId = aForId;
    claim.Tick = 0;
    Grant(claim, aTo);
    return true;
}

void HostClaims::OnGiveBack(const ActorGiveBackRequest& acRequest, ConnectionId_t aSender) noexcept
{
    // A give-back of another claim (passed on to a passenger, or an older one of the same game) is not the holder's word.
    const auto cIn = m_inClaim.find(acRequest.ActorId);
    if (cIn == m_inClaim.end() || cIn->second != acRequest.ClaimId || m_claims.at(cIn->second).To != aSender)
        return;
    GiveBack(acRequest.ClaimId, acRequest.ActorId, acRequest);
}

void HostClaims::Release(uint32_t aActorId) noexcept
{
    if (const auto cIn = m_inClaim.find(aActorId); cIn != m_inClaim.end())
        GiveBack(cIn->second, aActorId, {});
}

void HostClaims::End(uint32_t aClaimId) noexcept
{
    const auto cHeld = m_claims.find(aClaimId);
    if (cHeld == m_claims.end())
        return;
    const auto cMembers = cHeld->second.Claim.Members;
    for (const auto& cMember : cMembers)
        if (const auto cIn = m_inClaim.find(cMember.ActorId); cIn != m_inClaim.end() && cIn->second == aClaimId)
            GiveBack(aClaimId, cMember.ActorId, {});
}

void HostClaims::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    std::vector<uint32_t> held;
    for (const auto& [cClaimId, cHeld] : m_claims)
        if (cHeld.To == aPlayer)
            held.push_back(cClaimId);
    for (const uint32_t cClaimId : held)
        End(cClaimId);
}

void HostClaims::Forget(uint32_t aActorId) noexcept
{
    m_returning.erase(aActorId);
    const auto cIn = m_inClaim.find(aActorId);
    if (cIn == m_inClaim.end())
        return;
    const uint32_t cClaimId = cIn->second;
    m_inClaim.erase(cIn);
    auto& held = m_claims.at(cClaimId);
    std::erase_if(held.Claim.Members, [&](const Payloads::ClaimMember& acMember) { return acMember.ActorId == aActorId; });
    if (held.Claim.Members.empty())
    {
        const Payloads::Claim cClaim = held.Claim;
        m_claims.erase(cClaimId);
        Ended(cClaim);
        return;
    }
    End(cClaimId);
}

void HostClaims::GiveBack(uint32_t aClaimId, uint32_t aActorId, const Payloads::ActorGiveBack& acGiveBack) noexcept
{
    const auto cHeld = m_claims.find(aClaimId);
    const auto& cMembers = cHeld->second.Claim.Members;
    const auto cMember = std::ranges::find(cMembers, aActorId, &Payloads::ClaimMember::ActorId);
    NotifyActorGiveBack notify;
    static_cast<Payloads::ActorGiveBack&>(notify) = acGiveBack;
    notify.ActorId = aActorId;
    notify.ClaimId = aClaimId;
    notify.ForId = cHeld->second.Claim.ForId; // the killer of a killed one: whom the claim was for, never the sender's word
    notify.Taken = cMember->Take;
    notify.Killed = acGiveBack.Killed && cMember->Take && cMember->DeathOnGiveBack;
    m_inClaim.erase(aActorId);
    if (cMember->Take)
    {
        m_characters.SetOwner(aActorId, m_players.Host());
        m_returning.insert(aActorId);
    }
    notify.Ended = std::ranges::none_of(cMembers, [&](const Payloads::ClaimMember& acOther) {
        const auto cIn = m_inClaim.find(acOther.ActorId);
        return cIn != m_inClaim.end() && cIn->second == aClaimId;
    });
    spdlog::info("HostClaims: {:x} back from claim {}{}{}", aActorId, aClaimId, notify.Killed ? ", killed" : "", notify.Ended ? ", its last" : "");
    m_players.SendToAll(notify);
    if (!notify.Ended)
        return;
    const Payloads::Claim cClaim = cHeld->second.Claim;
    m_claims.erase(cHeld);
    Ended(cClaim);
}

void HostClaims::Ended(const Payloads::Claim& acClaim) noexcept
{
    if (const auto cRule = m_rules.find(acClaim.Kind); cRule != m_rules.end() && cRule->second.Ended)
        cRule->second.Ended(acClaim);
}
