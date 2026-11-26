// CharacterService: spoken lines and subtitles.
#include <characters/CharacterService.h>
#include <core/TransportService.h>
#include <core/SessionService.h>

#include <core/FormIdComponent.h>
#include <core/RemoteComponent.h>

#include <dialogue/DialogueEvent.h>
#include <dialogue/SubtitleEvent.h>


#include <ports/IActorBridge.h>
#include <ports/IFormBridge.h>
#include <ports/IPlayerBridge.h>


#include <core/World.h>
#include <core/SessionIds.h>
#include <core/FormIds.h>

#include <Messages/Relayed.h>
#include <characters/CharacterInternal.h>

void CharacterService::OnDialogueEvent(const DialogueEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.ActorID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto sessionIdRes = Utils::GetSessionId(*entityIt);
    if (!sessionIdRes)
    {
        spdlog::error("{}: session id not found for form id {:X}", __FUNCTION__, acEvent.ActorID);
        return;
    }

    DialogueRequest request{};
    request.SessionId = sessionIdRes.value();
    request.SoundFilename = acEvent.VoiceFile;

    m_transport.Send(request);
}

void CharacterService::OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.SessionId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.SessionId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);
    // A copy not loaded yet has no process, which Actor::SpeakSound (37542) reads unchecked.
    if (!m_actorBridge.ActorHas3D(formIdComponent.Id))
        return;

    spdlog::debug("Voice: copy {:X} says its owner's {}", formIdComponent.Id, acMessage.SoundFilename.c_str());
    m_actorBridge.ActorStopCurrentDialogue(formIdComponent.Id);
    m_actorBridge.ActorSpeakSound(formIdComponent.Id, std::string(acMessage.SoundFilename.c_str()));
}

void CharacterService::OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.SpeakerID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto sessionIdRes = Utils::GetSessionId(*entityIt);
    if (!sessionIdRes)
    {
        spdlog::error("{}: session id not found for form id {:X}", __FUNCTION__, acEvent.SpeakerID);
        return;
    }

    SubtitleRequest request{};
    request.SessionId = sessionIdRes.value();
    request.Text = acEvent.Text;
    request.TopicFormId = acEvent.TopicFormID;
    request.Force = acEvent.Force;

    m_transport.Send(request);
}

void CharacterService::OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.SessionId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.SessionId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    // The owner's subtitle text over the copy (its voice comes through NotifyDialogue).
    m_actorBridge.ActorShowSubtitle(formIdComponent.Id, std::string(acMessage.Text.c_str()), acMessage.TopicFormId, acMessage.Force);
}
