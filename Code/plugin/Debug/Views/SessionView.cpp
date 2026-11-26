#include <DebugService.h>

#include <core/World.h>

#include <imgui.h>
#include <game/HostService.h>
#include <core/SessionService.h>

void DebugService::DrawSessionView()
{
    if (!m_transport.IsConnected())
        return;

    ImGui::Begin("Session");

    auto& hostService = m_world.GetHostService();
    ImGui::BulletText(hostService.IsHosting() ? "You (host)" : "You");

    const auto& players = m_world.GetSessionService().GetPlayers();
    for (const auto& [cId, cPlayer] : players)
    {
        ImGui::PushID(static_cast<int>(cId));

        std::string name = cPlayer.Name;
        if (cPlayer.IsHost)
            name += " (host)";
        ImGui::BulletText("%s", name.c_str());

        if (hostService.IsHosting())
        {
            ImGui::SameLine(200);
            if (ImGui::Button("Kick"))
                hostService.KickPlayer(cId);
        }

        ImGui::PopID();
    }
    if (players.empty())
        ImGui::BulletText("<No one else here>");

    ImGui::End();
}
