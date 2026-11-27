#include <DebugService.h>
#include <core/TransportService.h>

#include <imgui.h>

void DebugService::DrawNetworkView()
{
    if (m_transport.IsConnected())
    {
        ImGui::Begin("Network");

        auto stats = m_transport.GetStatistics();
        float protocolSent = float(stats.SentBytes) / 1024.f;

        // Fill an array of contiguous float values to plot
        // Tip: If your float aren't contiguous but part of a structure, you can pass a pointer to your first float
        // and the sizeof() of your structure in the "stride" parameter.
        static float values[90] = {};
        static int values_offset = 0;
        static double refresh_time = 0.0;
        if (refresh_time == 0.0)
            refresh_time = ImGui::GetTime();
        while (refresh_time < ImGui::GetTime()) // Create data at fixed 60 Hz rate for the demo
        {
            values[values_offset] = protocolSent;
            values_offset = (values_offset + 1) % IM_ARRAYSIZE(values);
            refresh_time += 1.0f;
        }

        ImGui::PlotLines("Lines", values, IM_ARRAYSIZE(values), values_offset, nullptr, 0.f, FLT_MAX, ImVec2(0, 160.0f));

        ImGui::End();
    }

    // online indicator
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(23.f, 23.f), ImVec2(50.f, 50.f), m_transport.IsConnected() ? ImColor(0, 230, 64) : ImColor(240, 52, 52));
}
