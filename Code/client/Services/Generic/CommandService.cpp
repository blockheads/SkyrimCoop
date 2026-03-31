
#include <Services/CommandService.h>
#include <Services/TransportService.h>

#include <Systems/ModSystem.h>
#include <World.h>

#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>

#include <Events/SetTimeCommandEvent.h>

#include <Messages/TeleportCommandRequest.h>
#include <Messages/TeleportCommandResponse.h>
#include "Messages/SetTimeCommandRequest.h"

#include <Structs/GridCellCoords.h>

CommandService::CommandService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_formBridge(aWorld.ctx().at<IFormBridge&>())
    , m_playerBridge(aWorld.ctx().at<IPlayerBridge&>())
{
    m_setTimeConnection = aDispatcher.sink<SetTimeCommandEvent>().connect<&CommandService::OnSetTimeCommand>(this);
    m_teleportConnection = aDispatcher.sink<TeleportCommandResponse>().connect<&CommandService::OnTeleportCommandResponse>(this);
}

void CommandService::OnSetTimeCommand(const SetTimeCommandEvent& acEvent) const noexcept
{
    SetTimeCommandRequest request{};
    request.Hours = acEvent.Hours;
    request.Minutes = acEvent.Minutes;
    request.PlayerId = acEvent.PlayerId;
    m_transport.Send(request);
}

void CommandService::OnTeleportCommandResponse(const TeleportCommandResponse& acMessage) noexcept
{
    spdlog::info("TeleportCommandResponse: {:X}:{:X} at position {}, {}, {}", acMessage.CellId.ModId, acMessage.CellId.BaseId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
    auto& modSystem = m_world.GetModSystem();

    uint32_t cellId = modSystem.GetGameId(acMessage.CellId);

    if (!m_formBridge.FormExists(cellId))
    {
        // Try to resolve by worldspace + grid coords
        const uint32_t worldSpaceId = modSystem.GetGameId(acMessage.WorldSpaceId);
        if (worldSpaceId != 0)
        {
            GridCellCoords coordinates = GridCellCoords::CalculateGridCellCoords(acMessage.Position);
            cellId = m_formBridge.LookupCellByCoords(worldSpaceId, coordinates.X, coordinates.Y);
        }

        if (cellId == 0 || !m_formBridge.FormExists(cellId))
        {
            spdlog::error("Failed to fetch cell to teleport to.");
            m_world.GetOverlayService().SendSystemMessage("Teleporting to player failed.");
            return;
        }
    }

    m_playerBridge.MovePlayerTo(cellId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
}
