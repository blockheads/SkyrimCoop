#pragma once

#include <Bridges/IBugReportBridge.h>

// Writes a part under My Games/Skyrim Special Edition/SkyrimCoop/reports.
struct BugReportBridge final : IBugReportBridge
{
    std::string Write(const BugReportPart& acPart) noexcept override;
    void Export(const std::string& acPlayer, std::function<void(const std::string&)> aDone) noexcept override;
};
