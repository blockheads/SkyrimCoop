#pragma once

#include <Bridges/IBugReportBridge.h>

#include <vector>

// The parts a game wrote, kept instead of written.
struct FakeBugReports final : IBugReportBridge
{
    std::string Write(const BugReportPart& acPart) noexcept override
    {
        Parts.push_back(acPart);
        return "reports/" + acPart.Id;
    }

    void Export(const std::string& acPlayer, std::function<void(const std::string&)> aDone) noexcept override
    {
        Exports.push_back(acPlayer);
        aDone("SkyrimCoop-" + acPlayer + ".zip");
    }

    std::vector<BugReportPart> Parts;
    std::vector<std::string> Exports;
};
