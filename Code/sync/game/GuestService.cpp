#include <game/GuestService.h>
#include <scripts/HostSight.h>

#include <ports/IGuestEngine.h>
#include <core/ConnectedEvent.h>
#include <core/DisconnectedEvent.h>
#include <core/UpdateEvent.h>
#include <Messages/Relayed.h>
#include <scripts/PlayerNatives.h>
#include <ports/IActorBridge.h>
#include <time/GuestClock.h>
#include <characters/GuestCopies.h>
#include <physics/GuestBodies.h>
#include <time/GuestWeather.h>
#include <crime/GuestEye.h>
#include <travel/GuestLeash.h>
#include <combat/GuestTraps.h>
#include <ports/IPlayerBridge.h>
#include <world/GuestWorldState.h>
#include <time/GuestWait.h>
#include <dialogue/GuestDialogue.h>
#include <join/GuestRecord.h>
#include <join/GuestSaves.h>
#include <scripts/GuestScene.h>
#include <physics/IBodyBridge.h>
#include <join/IJoinBridge.h>
#include <core/RunnerService.h>
#include <core/TransportService.h>
#include <core/ScriptValues.h>
#include <core/World.h>
#include <core/Profile.h>

// The parts, after the engine they apply through.
struct GuestService::Guest
{
    Guest(World& aWorld, entt::dispatcher& aDispatcher, std::unique_ptr<IGuestEngine> apEngine) noexcept
        : pEngine(std::move(apEngine))
        , Engine(*pEngine)
        , Clock(aWorld, aDispatcher, Engine)
        , Weather(aWorld, aDispatcher, Engine)
        , Eye(aDispatcher, Engine)
        , WorldState(aWorld, aDispatcher, Engine)
        , Dialogue(aWorld, aDispatcher, aWorld.ctx().at<DialogueService>())
        , Record(aWorld, aDispatcher, aWorld.ctx().at<IJoinBridge&>())
        , Saves(aWorld, aDispatcher, aWorld.ctx().at<IJoinBridge&>())
        , Bodies(aWorld, Engine)
        , Wait(aWorld, aDispatcher, Engine)
        , Scene(aWorld, aDispatcher, Engine)
        , Copies(aWorld, aDispatcher, aWorld.ctx().at<IActorBridge&>())
        , Leash(aWorld, aDispatcher, aWorld.ctx().at<IPlayerBridge&>())
        , Traps(aWorld, aDispatcher)
    {
    }

    std::unique_ptr<IGuestEngine> pEngine;
    IGuestEngine& Engine;
    GuestClock Clock;
    GuestWeather Weather;
    GuestEye Eye;
    GuestWorldState WorldState;
    GuestDialogue Dialogue;
    GuestRecord Record;
    GuestSaves Saves;
    GuestBodies Bodies;
    GuestWait Wait;
    GuestScene Scene;
    GuestCopies Copies;
    GuestLeash Leash;
    GuestTraps Traps;
};

GuestService::GuestService(World& aWorld, entt::dispatcher& aDispatcher, MakeEngine_t aMakeEngine) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_makeEngine(std::move(aMakeEngine))
{
    Listen<ConnectedEvent, &GuestService::OnConnected>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &GuestService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &GuestService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<NotifyRoutedCall, &GuestService::OnPlayerNative>(m_connections, aDispatcher, this);
}

GuestService::~GuestService() noexcept = default;

void GuestService::OnConnected(const ConnectedEvent&) noexcept
{
    // The host joins its own session too: its world is its own.
    if (m_world.IsHosting())
        return;
    m_pGuest = std::make_unique<Guest>(m_world, m_dispatcher, m_makeEngine());
    PlayerNatives::SetGuest(true);
}

void GuestService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    PlayerNatives::SetGuest(false);
    if (m_pGuest)
        m_pGuest->Dialogue.OnLeft();
    m_pGuest.reset();
}

void GuestService::OnPlayerNative(const NotifyRoutedCall& acMessage) noexcept
{
    if (!m_pGuest || acMessage.To != CallTo::kOwner)
        return;
    // The host's sight questions come several times a second while a script polls (HostSight).
    spdlog::log(HostSight::IsSightReply(acMessage.ReplyId) ? spdlog::level::debug : spdlog::level::info,
                "GuestService: the host's script runs {}.{} for this game's player", acMessage.Call.Class, acMessage.Call.Function);
    // One returning a value: the host's script waits for it.
    const uint32_t cReplyId = acMessage.ReplyId;
    std::function<void(LocalValue)> onReturn;
    if (cReplyId)
        onReturn = [this, cReplyId](LocalValue aResult) {
            m_world.GetRunner().Queue([this, cReplyId, cResult = std::move(aResult)] { SendNativeResult(cReplyId, cResult); });
        };
    if (m_pGuest->Engine.RunPlayerNative(acMessage, std::move(onReturn)))
        return;
    spdlog::warn("GuestService: {}.{} did not run", acMessage.Call.Class, acMessage.Call.Function);
    if (cReplyId)
        SendNativeResult(cReplyId, {});
}

void GuestService::SendNativeResult(uint32_t aReplyId, const LocalValue& acResult) noexcept
{
    PlayerNativeResultRequest request;
    request.ReplyId = aReplyId;
    request.Result = Utils::ToScriptValue(acResult);
    m_world.GetTransport().Send(request);
}

void GuestService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PROFILE_ZONE("GuestService::OnUpdate");
    if (!m_pGuest)
        return;
    m_pGuest->Clock.OnUpdate(acEvent);
    m_pGuest->Weather.OnUpdate(acEvent);
    m_pGuest->WorldState.OnUpdate(acEvent);
    m_pGuest->Record.OnUpdate(acEvent);
    // The host's rests a world state row placed: simulated again, asleep (BodyService plays the rest).
    m_pGuest->Engine.FreePlaced();
    m_pGuest->Wait.OnUpdate();
    m_pGuest->Leash.OnUpdate(acEvent);
}
