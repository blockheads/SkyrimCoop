/// ClientReplayHarness.cpp — headless test wrapper implementation.
///
/// IMPORTANT: This file must NOT include any SKSE header.
/// It creates a ReplayBridge and injects it into an entt::registry context,
/// mirroring exactly how World.cpp does this for the live SkyrimBridge.

// TiltedPhoques must be in scope before any encoding struct headers
// (ActorValues, Inventory, etc. use TiltedPhoques::Map/String without re-including).
#include <optional>
#include <mutex>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include "ClientReplayHarness.h"

#include <Bridges/ReplayBridge.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Bridges/IEventBridge.h>
#include <Bridges/IWorldBridge.h>

ClientReplayHarness::ClientReplayHarness()
{
    // Create ReplayBridge — this is the SKSE-free implementation of all 5 bridge interfaces.
    m_pBridge = std::make_unique<ReplayBridge>();

    // Inject all five bridge interfaces into the registry context, mirroring World.cpp:
    //   ctx().emplace<IActorBridge&>(*m_pSkyrimBridge);
    // Services access bridges via ctx().at<IXxxBridge&>() — the same lookup works here.
    //
    // IMPORTANT: With multiple inheritance, entt 3.10.0 ctx().emplace<T&>(obj) stores
    // the raw pointer of obj reinterpreted as T. To get the correct virtual dispatch,
    // we must explicitly cast to the interface type before storing.
    IActorBridge&  actorRef  = *m_pBridge;
    IFormBridge&   formRef   = *m_pBridge;
    IPlayerBridge& playerRef = *m_pBridge;
    IEventBridge&  eventRef  = *m_pBridge;
    IWorldBridge&  worldRef  = *m_pBridge;
    m_registry.ctx().emplace<IActorBridge&>(actorRef);
    m_registry.ctx().emplace<IFormBridge&>(formRef);
    m_registry.ctx().emplace<IPlayerBridge&>(playerRef);
    m_registry.ctx().emplace<IEventBridge&>(eventRef);
    m_registry.ctx().emplace<IWorldBridge&>(worldRef);
}

ClientReplayHarness::~ClientReplayHarness() = default;

void ClientReplayHarness::LoadFixture(const std::string& aPath)
{
    m_pBridge->LoadRecording(aPath);
}

void ClientReplayHarness::Tick(double aDelta)
{
    // Pump any recorded event entries (quest start/stop, activate, lock change).
    m_pBridge->PumpEvents();
    // No real dispatcher — headless tick just exercises bridge event injection.
    (void)aDelta;
}

entt::registry& ClientReplayHarness::GetRegistry() noexcept
{
    return m_registry;
}

ReplayBridge& ClientReplayHarness::GetReplayBridge() noexcept
{
    return *m_pBridge;
}
