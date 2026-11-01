#include <Services/DebugService.h>

#include <Diagnostics/Snapshot.h>

#include <imgui.h>

// The last ten seconds as a bug report keeps the last minute (Diagnostics::PerfStatus): frames, our update, messages.
void DebugService::DrawPerfView()
{
    ImGui::Begin("Performance");
    // Twice a second: it sorts the window's frames and reads the message trail.
    static nlohmann::json s_perf;
    static double s_refreshed = -1.0;
    if (ImGui::GetTime() - s_refreshed > 0.5)
    {
        s_perf = Diagnostics::PerfStatus(false, 10000);
        s_refreshed = ImGui::GetTime();
    }
    const auto cNumber = [](const nlohmann::json& acValue) { return acValue.is_number() ? acValue.get<double>() : 0.0; };
    const auto& cFrame = s_perf["frame_ms"];
    const auto& cUpdate = s_perf["update_ms"];
    const double cSeconds = std::max(cNumber(s_perf["window_s"]), 0.001);
    ImGui::Text("%.1f fps over %.1fs", cNumber(s_perf["frames"]) / cSeconds, cSeconds);
    ImGui::Text("frame ms  p50 %.1f  p95 %.1f  p99 %.1f  max %.1f", cNumber(cFrame["p50"]), cNumber(cFrame["p95"]), cNumber(cFrame["p99"]),
                cNumber(cFrame["max"]));
    ImGui::Text("our update ms  p50 %.2f  p95 %.2f  max %.1f", cNumber(cUpdate["p50"]), cNumber(cUpdate["p95"]), cNumber(cUpdate["max"]));
    if (s_perf.contains("memory_mb"))
        ImGui::Text("memory  %.0f MB working set, %.0f MB private", cNumber(s_perf["memory_mb"]["working_set"]),
                    cNumber(s_perf["memory_mb"]["private"]));
    ImGui::Separator();
    ImGui::Text("messages %.1f KB/s", cNumber(s_perf["messages"]["bytes"]) / cSeconds / 1024.0);
    if (ImGui::BeginTable("messages", 4))
    {
        ImGui::TableSetupColumn("message");
        ImGui::TableSetupColumn("way");
        ImGui::TableSetupColumn("/s");
        ImGui::TableSetupColumn("KB/s");
        ImGui::TableHeadersRow();
        for (const auto& cRow : s_perf["messages"]["top"])
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(cRow["name"].get<std::string>().c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(cRow["way"].get<std::string>().c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", cNumber(cRow["count"]) / cSeconds);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", cNumber(cRow["bytes"]) / cSeconds / 1024.0);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
