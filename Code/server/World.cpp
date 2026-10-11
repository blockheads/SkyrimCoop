#include <World.h>
#include <Components.h>

#include <Services/CharacterService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/ActorValueService.h>
#include <Services/AdminService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/OverlayService.h>
#include <Services/CommandService.h>
#include <Services/StringCacheService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/ScriptService.h>

#include <es_loader/ESLoader.h>

namespace Server
{

World::World()
{
    m_spAdminService = std::make_shared<AdminService>(*this, m_dispatcher);
    spdlog::default_logger()->sinks().push_back(std::static_pointer_cast<spdlog::sinks::sink>(m_spAdminService));

    ctx().emplace<CharacterService>(*this, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher);
    ctx().emplace<CalendarService>(*this, m_dispatcher);
    ctx().emplace<ObjectService>(*this, m_dispatcher);
    ctx().emplace<ModsComponent>();
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<PartyService>(*this, m_dispatcher);
    ctx().emplace<ActorValueService>(*this, m_dispatcher);
    ctx().emplace<InventoryService>(*this, m_dispatcher);
    ctx().emplace<MagicService>(*this, m_dispatcher);
    ctx().emplace<OverlayService>(*this, m_dispatcher);
    ctx().emplace<CommandService>(*this, m_dispatcher);
    ctx().emplace<StringCacheService>(*this, m_dispatcher);
    ctx().emplace<CombatService>(*this, m_dispatcher);
    ctx().emplace<WeatherService>(*this, m_dispatcher);

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

    ctx().emplace<CharacterService>(*this, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher);
    ctx().emplace<CalendarService>(*this, m_dispatcher);
    ctx().emplace<ObjectService>(*this, m_dispatcher);
    ctx().emplace<ModsComponent>();
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<PartyService>(*this, m_dispatcher);
    ctx().emplace<ActorValueService>(*this, m_dispatcher);
    ctx().emplace<InventoryService>(*this, m_dispatcher);
    ctx().emplace<MagicService>(*this, m_dispatcher);
    ctx().emplace<OverlayService>(*this, m_dispatcher);
    ctx().emplace<CommandService>(*this, m_dispatcher);
    ctx().emplace<StringCacheService>(*this, m_dispatcher);
    ctx().emplace<CombatService>(*this, m_dispatcher);
    ctx().emplace<WeatherService>(*this, m_dispatcher);

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
    ctx().erase<WeatherService>();
    ctx().erase<CombatService>();
    ctx().erase<StringCacheService>();
    ctx().erase<CommandService>();
    ctx().erase<OverlayService>();
    ctx().erase<MagicService>();
    ctx().erase<InventoryService>();
    ctx().erase<ActorValueService>();
    ctx().erase<PartyService>();
    ctx().erase<QuestService>();
    ctx().erase<ObjectService>();
    ctx().erase<CalendarService>();
    ctx().erase<PlayerService>();
    ctx().erase<CharacterService>();
}

} // namespace Server
