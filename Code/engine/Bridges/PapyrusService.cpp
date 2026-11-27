#include <Bridges/PapyrusService.h>
#include <core/EngineNames.h>
#include <HotReload/Handoff.h>

#include <mutex>
#include <sstream>
#include <unordered_map>

namespace
{
constexpr char kHandoffKey[] = "papyrus_natives";

std::mutex s_lock;
std::unordered_map<std::string, void*> s_functions; // by NameKey("Namespace::Name")
} // namespace

namespace PapyrusNatives
{
void Add(const std::string& acNamespace, const std::string& acFunction, void* apFunction) noexcept
{
    std::scoped_lock _{s_lock};
    s_functions[NameKey(acNamespace + "::" + acFunction)] = apFunction;
}

const void* Get(const std::string& acNamespace, const std::string& acFunction) noexcept
{
    std::scoped_lock _{s_lock};
    const auto itor = s_functions.find(NameKey(acNamespace + "::" + acFunction));
    return itor != s_functions.end() ? itor->second : nullptr;
}

void HandOff() noexcept
{
    std::ostringstream lines;
    {
        std::scoped_lock _{s_lock};
        for (const auto& [cKey, cpFunction] : s_functions)
            lines << cKey.c_str() << ' ' << std::hex << reinterpret_cast<uintptr_t>(cpFunction) << '\n';
    }
    CoopReload::HandOff(kHandoffKey, lines.str());
}

// "<namespace>::<name> <address>" lines.
void TakeHandOff() noexcept
{
    const auto cHandoff = CoopReload::TakeHandoff(kHandoffKey);
    if (!cHandoff)
        return;
    std::istringstream lines(*cHandoff);
    std::string key;
    uintptr_t address;
    std::scoped_lock _{s_lock};
    while (lines >> key >> std::hex >> address)
        s_functions[NameKey(key)] = reinterpret_cast<void*>(address);
    spdlog::info("Papyrus natives from the previous load: {}", s_functions.size());
}
} // namespace PapyrusNatives
