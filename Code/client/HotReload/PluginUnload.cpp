#include <TiltedOnlinePCH.h>

#include <TiltedOnlineApp.h>
#include <World.h>
#include <Components.h>
#include <TestHarness/TestHarness.h>
#include <Games/References.h>
#include <Bridges/IActorBridge.h>
#include <Services/PapyrusService.h>

#include <Client.hpp>
#include <FunctionHook.hpp>
#include <SkseRipAllocator.h>

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

// Hot reload (scripts/coop_test.py reload), test builds only: the resident loader
// (Code/hotreload/Loader.cpp) calls these to swap this DLL inside a running game. Unload runs on
// the main thread from the loader's own main-loop hook, so none of our frames are on that stack;
// the loader then waits until no other thread is inside our code before FreeLibrary.

// The actor memory layout the engine's existing actors carry: a reload that changes it needs a
// game restart.
extern "C" __declspec(dllexport) uint32_t SkyrimCoop_ExtensionLayout()
{
    return static_cast<uint32_t>(sizeof(ExActor)) << 16 | static_cast<uint32_t>(sizeof(ExPlayerCharacter) & 0xFFFF);
}

// The engine's main loop, which the loader hooks on top of ours.
extern "C" __declspec(dllexport) void* SkyrimCoop_MainLoopAddress()
{
    POINTER_SKYRIMSE(void, s_mainLoop, 36564);
    return s_mainLoop.GetPtr();
}

extern "C" __declspec(dllexport) bool SkyrimCoop_Unload()
{
    spdlog::info("Hot reload: unloading the plugin");

    // Copies leave the game: the next load spawns them again from the session.
    if (entt::locator<World>::has_value())
    {
        World& world = World::Get();
        auto& actors = world.GetActorBridge();
        for (const auto cEntity : world.view<FormIdComponent, RemoteComponent>())
        {
            const uint32_t cFormId = world.get<FormIdComponent>(cEntity).Id;
            if (!actors.ActorExists(cFormId))
                continue;
            if (actors.IsRemotePlayer(cFormId) || actors.IsActorTemporary(cFormId))
                actors.DeleteActor(cFormId);
            else
                actors.SetActorRemote(cFormId, false);
        }
        world.ctx().at<PapyrusService>().HandOffFunctions();
        world.GetTransport().Close();
        entt::locator<World>::reset();
    }

    TestHarness::Shutdown();

    // Nothing in the game may point into this DLL afterwards.
    TiltedPhoques::FunctionHookManager::GetInstance().RemoveAllHooks();
    TiltedPhoques::RestorePatches();
    TiltedPhoques::RunUnloadCallbacks();
    CoopSkse::HandOffJit(); // Only the patches just restored branched into it.

    // Our threads: the hang watchdog (crash handler), the network library's, spdlog's flusher.
    g_appInstance.reset();
    TiltedPhoques::ShutdownNetworkWorkers();
    spdlog::info("Hot reload: unloaded");
    spdlog::shutdown();
    return true;
}
