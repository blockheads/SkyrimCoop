#include "ReportPaths.h"

#include <chrono>
#include <format>
#include <random>

namespace Diagnostics
{
std::string ReportId(const std::tm& acLocal, uint32_t aRandom)
{
    return std::format("{:04}-{:02}-{:02}-{:02}{:02}-{:03x}", acLocal.tm_year + 1900, acLocal.tm_mon + 1, acLocal.tm_mday, acLocal.tm_hour,
                       acLocal.tm_min, aRandom & 0xFFF);
}

std::string NewReportId()
{
    const std::time_t cNow = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &cNow);
#else
    localtime_r(&cNow, &local);
#endif
    return ReportId(local, std::random_device{}());
}

std::string SafeName(std::string_view aText, std::string_view aEmpty)
{
    std::string name;
    for (const char c : aText)
    {
        if (name.size() == 48)
            break;
        const bool cKept = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        name += cKept ? c : '_';
    }
    if (name.find_first_not_of('_') == std::string::npos)
        return std::string(aEmpty);
    return name;
}

std::string StoredSaveName(std::string_view aStem, int64_t aWriteTime)
{
    return std::format("{}-{:x}", SafeName(aStem, "save"), static_cast<uint64_t>(aWriteTime));
}

std::filesystem::path PartFolder(const std::filesystem::path& acRoot, std::string_view aId, bool aHost, std::string_view aPlayer)
{
    return acRoot / SafeName(aId, "report") / std::format("{}-{}", aHost ? "host" : "guest", SafeName(aPlayer));
}
} // namespace Diagnostics
