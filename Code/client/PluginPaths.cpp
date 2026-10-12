#include <TiltedOnlinePCH.h>
#include <PluginPaths.h>

#include <algorithm>

namespace PluginPaths
{
static std::filesystem::path s_directory;

void SetDirectory(const std::filesystem::path& acDirectory)
{
    s_directory = acDirectory;
}

const std::filesystem::path& GetDirectory()
{
    return s_directory;
}

std::string InstanceSuffix()
{
    char instance[32]{};
    const DWORD cLength = GetEnvironmentVariableA("SKYRIMCOOP_INSTANCE", instance, sizeof(instance));
    if (cLength == 0 || cLength >= sizeof(instance))
        return {};
    const std::string cName(instance);
    const bool cValid = std::all_of(cName.begin(), cName.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
    return cValid ? "-" + cName : std::string{};
}
} // namespace PluginPaths
