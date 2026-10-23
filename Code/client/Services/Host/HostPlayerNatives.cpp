#include <Services/Host/HostPlayerNatives.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/NotifyPlayerNative.h>
#include <Messages/NotifyPlayerNativeResult.h>
#include <Messages/PlayerNativeRequest.h>
#include <Messages/PlayerNativeResultRequest.h>
#include <Messages/NotifyWorldCall.h>
#include <Messages/WorldCallRequest.h>

#include <spdlog/spdlog.h>

#include <vector>

HostPlayerNatives::HostPlayerNatives(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers,
                                     const HostCharacters& acCharacters) noexcept
    : m_network(aNetwork), m_players(acPlayers), m_characters(acCharacters)
{
    aAuthority.On(*this, &HostPlayerNatives::OnPlayerNative);
    aAuthority.On(*this, &HostPlayerNatives::OnPlayerNativeResult);
    aAuthority.On(*this, &HostPlayerNatives::OnWorldCall);
}

void HostPlayerNatives::OnPlayerNative(const PlayerNativeRequest& acRequest, ConnectionId_t aSender) noexcept
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
    NotifyPlayerNative notify;
    static_cast<Payloads::PlayerNative&>(notify) = acRequest;
    if (acRequest.ReplyId)
        m_waiting[acRequest.ReplyId] = cOwner;
    m_network.Send(cOwner, notify);
}

void HostPlayerNatives::OnPlayerNativeResult(const PlayerNativeResultRequest& acRequest,
                                             ConnectionId_t aSender) noexcept
{
    const auto it = m_waiting.find(acRequest.ReplyId);
    if (it == m_waiting.end() || it->second != aSender)
    {
        spdlog::warn("HostPlayerNatives: {:x} answered {}, which it was not asked", aSender, acRequest.ReplyId);
        return;
    }
    m_waiting.erase(it);
    Answer(acRequest.ReplyId, acRequest.Result);
}

void HostPlayerNatives::OnWorldCall(const WorldCallRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    const uint32_t cCharacter = aSender != cHost ? m_characters.PlayerCharacterOf(aSender) : 0;
    if (!cHost || !cCharacter)
    {
        spdlog::warn("HostPlayerNatives: {:x} has no character to run {}.{} for", aSender, acRequest.Call.Class, acRequest.Call.Function);
        return;
    }
    NotifyWorldCall notify;
    notify.SessionId = cCharacter;
    notify.Call = acRequest.Call;
    m_network.Send(cHost, notify);
}

void HostPlayerNatives::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
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
