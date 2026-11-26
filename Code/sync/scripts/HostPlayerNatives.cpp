#include <scripts/HostPlayerNatives.h>

#include <core/HostRouter.h>
#include <characters/HostCharacters.h>
#include <core/HostNetwork.h>
#include <core/HostPlayers.h>

#include <Messages/Relayed.h>

#include <spdlog/spdlog.h>

#include <vector>

HostPlayerNatives::HostPlayerNatives(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers,
                                     const HostCharacters& acCharacters) noexcept
    : m_network(aNetwork), m_players(acPlayers), m_characters(acCharacters)
{
    // A quest one per player, learned by one game (system-quests.md): every other learns it.
    aRouter.Relay<SystemQuestsRequest, NotifySystemQuests>(HostRouter::Delivery::FromAnyone);
    aRouter.On(*this, &HostPlayerNatives::OnCall);
    aRouter.On(*this, &HostPlayerNatives::OnPlayerNativeResult);
    aRouter.On(*this, &HostPlayerNatives::OnCallEnded);
}

void HostPlayerNatives::OnCall(const RoutedCallRequest& acRequest, ConnectionId_t aSender) noexcept
{
    switch (acRequest.To)
    {
    case CallTo::kOwner: ToOwner(acRequest, aSender); return;
    case CallTo::kHost: ToHost(acRequest, aSender); return;
    case CallTo::kOthers: ToOthers(acRequest, aSender); return;
    }
}

void HostPlayerNatives::ToOwner(const RoutedCallRequest& acRequest, ConnectionId_t aSender) noexcept
{
    // Only the host's game runs scripts for another player.
    if (aSender != m_players.Host())
    {
        spdlog::warn("HostPlayerNatives: {:x} is not the host and sent {}.{} for {:x}", aSender, acRequest.Call.Class,
                     acRequest.Call.Function, acRequest.SessionId);
        return;
    }
    const ConnectionId_t cOwner = m_characters.OwnerOf(acRequest.SessionId);
    if (cOwner == 0 || cOwner == aSender)
    {
        // Its player left before the script asked: a waiting script gets none.
        if (acRequest.ReplyId)
            Answer(acRequest.ReplyId, {});
        return;
    }
    NotifyRoutedCall notify;
    static_cast<Payloads::RoutedCall&>(notify) = acRequest;
    if (acRequest.ReplyId)
        m_waiting[acRequest.ReplyId] = cOwner;
    m_network.Send(cOwner, notify);
}

void HostPlayerNatives::OnPlayerNativeResult(const PlayerNativeResultRequest& acRequest,
                                             ConnectionId_t aSender) noexcept
{
    if (acRequest.SessionId)
    {
        const ConnectionId_t cGuest = m_characters.OwnerOf(acRequest.SessionId);
        if (aSender != m_players.Host() || !m_asked.erase({cGuest, acRequest.ReplyId}))
        {
            spdlog::warn("HostPlayerNatives: {:x} answered {} for {:x}, which nobody asked", aSender, acRequest.ReplyId, acRequest.SessionId);
            return;
        }
        NotifyPlayerNativeResult notify;
        static_cast<Payloads::PlayerNativeResult&>(notify) = acRequest;
        m_network.Send(cGuest, notify);
        return;
    }
    const auto it = m_waiting.find(acRequest.ReplyId);
    if (it == m_waiting.end() || it->second != aSender)
    {
        spdlog::warn("HostPlayerNatives: {:x} answered {}, which it was not asked", aSender, acRequest.ReplyId);
        return;
    }
    m_waiting.erase(it);
    Answer(acRequest.ReplyId, acRequest.Result);
}

void HostPlayerNatives::ToHost(const RoutedCallRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    const uint32_t cCharacter = aSender != cHost ? m_characters.PlayerCharacterOf(aSender) : 0;
    if (!cHost || !cCharacter)
    {
        spdlog::warn("HostPlayerNatives: {:x} has no character to run {}.{} for", aSender, acRequest.Call.Class, acRequest.Call.Function);
        if (acRequest.ReplyId && aSender != cHost)
        {
            NotifyPlayerNativeResult none; // its waiting script gets None
            none.ReplyId = acRequest.ReplyId;
            m_network.Send(aSender, none);
        }
        return;
    }
    NotifyRoutedCall notify;
    static_cast<Payloads::RoutedCall&>(notify) = acRequest;
    notify.SessionId = cCharacter;
    if (acRequest.ReplyId)
        m_asked.insert({aSender, acRequest.ReplyId});
    m_network.Send(cHost, notify);
}

void HostPlayerNatives::ToOthers(const RoutedCallRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const auto* cpPlayer = m_players.Find(aSender);
    if (!cpPlayer)
        return;
    NotifyRoutedCall notify;
    static_cast<Payloads::RoutedCall&>(notify) = acRequest;
    notify.Source = cpPlayer->Id;
    if (acRequest.Controls == CallControls::kTakes)
        m_controls[aSender].push_back(notify);
    else if (acRequest.Controls == CallControls::kGivesBack)
        m_controls.erase(aSender);
    m_players.SendToOthers(notify, aSender);
}

void HostPlayerNatives::SendHeld(ConnectionId_t aJoiner) const noexcept
{
    for (const auto& [cGame, cCalls] : m_controls)
        if (cGame != aJoiner)
            for (const NotifyRoutedCall& cCall : cCalls)
                m_network.Send(aJoiner, cCall);
}

void HostPlayerNatives::OnCallEnded(const CallEndedRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    const auto* cpPlayer = m_players.Find(aSender);
    if (!cpPlayer)
        return;
    NotifyCallEnded notify;
    static_cast<Payloads::CallEnded&>(notify) = acRequest;
    notify.Source = cpPlayer->Id;
    m_players.SendToOthers(notify, aSender);
}

void HostPlayerNatives::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    m_controls.erase(aPlayer);
    std::erase_if(m_asked, [aPlayer](const auto& acAsked) { return acAsked.first == aPlayer; });
    std::vector<uint32_t> unanswered;
    for (const auto& [cReplyId, cAsked] : m_waiting)
        if (cAsked == aPlayer)
            unanswered.push_back(cReplyId);
    for (const uint32_t cReplyId : unanswered)
    {
        m_waiting.erase(cReplyId);
        Answer(cReplyId, {});
    }
}

void HostPlayerNatives::Answer(uint32_t aReplyId, const ScriptValue& acResult) const noexcept
{
    NotifyPlayerNativeResult notify;
    notify.ReplyId = aReplyId;
    notify.Result = acResult;
    if (const ConnectionId_t cHost = m_players.Host())
        m_network.Send(cHost, notify);
}
