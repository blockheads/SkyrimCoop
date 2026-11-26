#pragma once

#include <memory>

class IActorBridge;
class IBodyBridge;
class IBugReportBridge;
class ICrimeBridge;
class IDevToolsBridge;
struct IAnimationBody;
class IDialogueBridge;
class IFormBridge;
class IGuestEngine;
class IHostEngine;
class IJoinBridge;
class IPlayerBridge;
class ISteamBridge;
class IWaitBridge;
class IWorldBridge;
struct World;

/**
 * @brief The engine a World runs on: every port, in one place. The plugin's is Skyrim (SkyrimEngine); the tests' is
 * the fake (tests/games/Seams.cpp). World owns it and builds every service on it, the same way in both.
 */
class IEngine
{
public:
    virtual ~IEngine() = default;

    virtual IActorBridge& Actors() noexcept = 0;
    virtual IFormBridge& Forms() noexcept = 0;
    virtual IPlayerBridge& Player() noexcept = 0;
    virtual IWorldBridge& WorldBridge() noexcept = 0;
    virtual IDialogueBridge& Dialogue() noexcept = 0;
    virtual ISteamBridge& Steam() noexcept = 0;
    virtual IJoinBridge& Join() noexcept = 0;
    virtual IWaitBridge& Wait() noexcept = 0;
    virtual IBodyBridge& Bodies() noexcept = 0;
    virtual ICrimeBridge& Crime() noexcept = 0;
    virtual IBugReportBridge& BugReports() noexcept = 0;
    virtual IDevToolsBridge& DevTools() noexcept = 0;
    virtual IAnimationBody& AnimationBodies() noexcept = 0;
    // The engine side of a guest's game, made as it joins; and of the host's, made as it starts hosting.
    virtual std::unique_ptr<IGuestEngine> MakeGuestEngine() = 0;
    virtual std::unique_ptr<IHostEngine> MakeHostEngine() = 0;

    // The layer above's own services, at their places among World's: the first ones (the UI), those after
    // CharacterService (the debug views), and their removal in the reverse places.
    virtual void AddFirstServices(World&) {}
    virtual void AddServicesAfterCharacters(World&) {}
    virtual void RemoveServicesAfterCharacters(World&) noexcept {}
    virtual void RemoveFirstServices(World&) noexcept {}
    // Every service is made; and the World is about to go.
    virtual void Started(World&) {}
    virtual void Stopping(World&) noexcept {}

    // One frame: the engine's own work around World::Step.
    virtual void Frame(World& aWorld) noexcept = 0;
};
