#include <World.h>
#include <Components.h>

#include <Services/QuestService.h>
#include <Services/AdminService.h>
#include <Services/ScriptService.h>

#include <es_loader/ESLoader.h>

namespace Server
{

World::World()
{
    m_spAdminService = std::make_shared<AdminService>(*this, m_dispatcher);
    spdlog::default_logger()->sinks().push_back(std::static_pointer_cast<spdlog::sinks::sink>(m_spAdminService));

    ctx().emplace<ModsComponent>();
    ctx().emplace<QuestService>(*this, m_dispatcher);

    ESLoader::ESLoader loader;
    // emplace loaded mods into modscomponent.
    m_recordCollection = loader.BuildRecordCollection();
    for (const auto& it : loader.GetLoadOrder())
    {
        ctx().emplace<ModsComponent>().AddServerMod(it);
    }

    // late initialize the ScriptService to ensure all components are valid
    m_pScriptService = TiltedPhoques::MakeUnique<ScriptService>(*this, m_dispatcher);
}

World::World(WorldTestMode)
{
    // Test/replay mode: emplace all services but skip ESLoader and ScriptService.
    // This allows ServerReplayHarness to construct a headless server World.
    m_spAdminService = std::make_shared<AdminService>(*this, m_dispatcher);
    spdlog::default_logger()->sinks().push_back(std::static_pointer_cast<spdlog::sinks::sink>(m_spAdminService));

    ctx().emplace<ModsComponent>();
    ctx().emplace<QuestService>(*this, m_dispatcher);

    // ScriptService is safe to create -- it just won't have any loaded scripts
    m_pScriptService = TiltedPhoques::MakeUnique<ScriptService>(*this, m_dispatcher);

    // Skip ESLoader::BuildRecordCollection() -- no mod data in test mode
    // Skip ScriptService creation -- m_pScriptService remains null (destructor handles nullptr)
}

World::~World()
{
    m_spAdminService->Detach();
    m_pScriptService.reset();

    // The services live in the registry's context, destroyed after m_dispatcher (one of our
    // members): their connections to it go first, newest service first.
    ctx().erase<QuestService>();
}

} // namespace Server
