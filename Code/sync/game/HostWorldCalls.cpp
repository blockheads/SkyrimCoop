#include <game/HostService.h>

#include <ports/IActorBridge.h>
#include <core/FormIds.h>
#include <Messages/Relayed.h>
#include <core/ScriptValues.h>
#include <core/SessionIds.h>
#include <core/TransportService.h>
#include <core/World.h>

void HostService::QueueWorldAnswer(uint32_t aReplyId, uint32_t aSessionId, LocalValue aResult) noexcept
{
    m_world.GetRunner().Queue([this, aReplyId, aSessionId, cResult = std::move(aResult)] {
        m_worldAnswers.push_back({aReplyId, aSessionId, cResult, std::chrono::steady_clock::now()});
    });
}

void HostService::SendWorldAnswers() noexcept
{
    if (!IsHosting())
    {
        m_worldAnswers.clear();
        return;
    }
    // An actor the call made (PlaceActorAtMe) goes by its session id once announced; 5 s at most.
    constexpr auto cActorWait = std::chrono::seconds(5);
    const auto cNow = std::chrono::steady_clock::now();
    std::erase_if(m_worldAnswers, [&](const WorldAnswer& acAnswer) {
        const auto* cpForm = std::get_if<LocalForm>(&acAnswer.Result);
        if (cpForm && IsCreatedForm(cpForm->FormId) && m_world.ctx().at<IActorBridge&>().ActorExists(cpForm->FormId) &&
            !Utils::GetSessionIdByFormId(cpForm->FormId) && cNow - acAnswer.Since < cActorWait)
            return false;
        PlayerNativeResultRequest result;
        result.ReplyId = acAnswer.ReplyId;
        result.SessionId = acAnswer.SessionId;
        result.Result = Utils::ToScriptValue(acAnswer.Result);
        m_world.GetTransport().Send(result);
        return true;
    });
}
