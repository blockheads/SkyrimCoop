#include "Commands.h"
#include "UpdateTrace.h"

#include <Actor.h>
#include <Services/Host/HostNetwork.h>
#include <Services/HostService.h>
#include <World.h>

#include <cstdint>
#include <format>

namespace TestHarness
{
namespace
{
std::string Address(const void* apObject)
{
    return std::format("{:#x}", reinterpret_cast<uintptr_t>(apObject));
}

// Addresses for an attached debugger's watchpoints, resolved afresh after a cold start or actor replacement.
nlohmann::json MemorySites(Harness&, const nlohmann::json& acRequest)
{
    if (!entt::locator<World>::has_value())
        return Error("the plugin world is not initialized");
    auto& world = World::Get();
    nlohmann::json sites = {{"world", Address(&world)},
                            {"world_size", sizeof(World)},
                            {"transport", Address(&world.GetTransport())},
                            {"transport_size", sizeof(TransportService)},
                            {"runner", Address(&world.GetRunner())},
                            {"host_network", Address(world.GetHostService().GetNetwork())},
                            {"host_network_size", sizeof(HostNetwork)}};
    const uint32_t cActorId = acRequest.value("actor", 0u);
    if (cActorId)
    {
        const Actor* pActor = Cast<Actor>(TESForm::GetById(cActorId));
        if (!pActor)
            return Error("no such actor");
        sites["actor"] = Address(pActor);
        sites["actor_base_slot"] = Address(&pActor->baseForm);
        sites["actor_base"] = Address(pActor->baseForm);
    }
    const auto cUpdates = UpdateTrace::Read();
    return {{"ok", true},
            {"thread", GetCurrentThreadId()},
            {"sites", std::move(sites)},
            {"updates",
             {{"total", cUpdates.Updates},
              {"frozen", cUpdates.Frozen},
              {"saving", cUpdates.Saving},
              {"loading", cUpdates.Loading},
              {"poll_overlaps", cUpdates.PollOverlaps},
              {"poll_concurrent", cUpdates.PollConcurrent},
              {"update_overlaps", cUpdates.UpdateOverlaps}}}};
}

constexpr Command cCommands[] = {{"memory_sites", false, &MemorySites}};
} // namespace

std::span<const Command> MemoryCommands()
{
    return cCommands;
}
} // namespace TestHarness
