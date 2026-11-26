#pragma once

#include <core/Listen.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

struct IActorIdentity;
struct IActorSpeech;

struct World;
struct TransportService;
struct DialogueEvent;
struct SubtitleEvent;

/**
 * @brief What actors say: an owner's voice lines and subtitles reach its copies.
 *
 * The owner's game hears the engine speak (DialogueEvent, SubtitleEvent) and tells the host; each copy says the
 * line and shows the subtitle (NotifyDialogue, NotifySubtitle). Conversations are DialogueService's.
 */
struct SpeechService
{
    SpeechService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorSpeech& aSpeech,
                  IActorIdentity& aActors) noexcept;
    ~SpeechService() noexcept = default;

    NO_COPY_MOVE(SpeechService);

    void OnDialogueEvent(const DialogueEvent& acEvent) noexcept;
    void OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept;
    void OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept;
    void OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept;

private:
    World& m_world;
    TransportService& m_transport;
    IActorSpeech& m_speech;
    IActorIdentity& m_actors;

    std::vector<entt::scoped_connection> m_connections;
};
