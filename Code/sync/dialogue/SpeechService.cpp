// What actors say: an owner's voice lines and subtitles reach its copies.
#include <dialogue/SpeechService.h>

#include <characters/IActorIdentity.h>
#include <dialogue/DialogueEvent.h>
#include <dialogue/IActorSpeech.h>
#include <dialogue/SubtitleEvent.h>

#include <core/FormIdComponent.h>
#include <core/RemoteComponent.h>
#include <core/SessionIds.h>
#include <core/TransportService.h>
#include <core/World.h>

#include <Messages/Relayed.h>

SpeechService::SpeechService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorSpeech& aSpeech,
                             IActorIdentity& aActors) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_speech(aSpeech)
    , m_actors(aActors)
{
    Listen<DialogueEvent, &SpeechService::OnDialogueEvent>(m_connections, aDispatcher, this);
    Listen<NotifyDialogue, &SpeechService::OnNotifyDialogue>(m_connections, aDispatcher, this);
    Listen<SubtitleEvent, &SpeechService::OnSubtitleEvent>(m_connections, aDispatcher, this);
    Listen<NotifySubtitle, &SpeechService::OnNotifySubtitle>(m_connections, aDispatcher, this);
}

void SpeechService::OnDialogueEvent(const DialogueEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    const auto cEntity = m_world.GetActorIndex().ByForm(acEvent.ActorID);
    if (cEntity == entt::null)
        return;

    auto sessionIdRes = Utils::GetSessionId(cEntity);
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

void SpeechService::OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept
{
    const auto cCopy = m_world.GetActorIndex().BySession<RemoteComponent, FormIdComponent>(acMessage.SessionId);
    if (cCopy == entt::null)
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.SessionId);
        return;
    }

    const auto formIdComponent = m_world.get<FormIdComponent>(cCopy);
    // A copy not loaded yet has no process, which Actor::SpeakSound (37542) reads unchecked.
    if (!m_actors.ActorHas3D(formIdComponent.Id))
        return;

    spdlog::debug("Voice: copy {:X} says its owner's {}", formIdComponent.Id, acMessage.SoundFilename.c_str());
    m_speech.ActorStopCurrentDialogue(formIdComponent.Id);
    m_speech.ActorSpeakSound(formIdComponent.Id, std::string(acMessage.SoundFilename.c_str()));
}

void SpeechService::OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    const auto cEntity = m_world.GetActorIndex().ByForm(acEvent.SpeakerID);
    if (cEntity == entt::null)
        return;

    auto sessionIdRes = Utils::GetSessionId(cEntity);
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

void SpeechService::OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept
{
    const auto cCopy = m_world.GetActorIndex().BySession<RemoteComponent, FormIdComponent>(acMessage.SessionId);
    if (cCopy == entt::null)
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.SessionId);
        return;
    }

    const auto formIdComponent = m_world.get<FormIdComponent>(cCopy);

    // The owner's subtitle text over the copy (its voice comes through NotifyDialogue).
    m_speech.ActorShowSubtitle(formIdComponent.Id, std::string(acMessage.Text.c_str()), acMessage.TopicFormId, acMessage.Force);
}
