#include <TiltedOnlinePCH.h>

#include <Events/PapyrusFunctionRegisterEvent.h>

#include <Services/PapyrusService.h>
#include <Games/EngineNames.h>
#include <HotReload/Handoff.h>

#include <sstream>

static constexpr char kHandoffKey[] = "papyrus_natives";

PapyrusService::PapyrusService(entt::dispatcher& aDispatcher) noexcept
{
    m_papyrusFunctionRegisterConnection = aDispatcher.sink<PapyrusFunctionRegisterEvent>().connect<&PapyrusService::HandlePapyrusFunctionEvent>(this);

    // "<namespace>::<name> <address>" lines.
    if (const auto cHandoff = CoopReload::TakeHandoff(kHandoffKey))
    {
        std::istringstream lines(*cHandoff);
        std::string key;
        uintptr_t address;
        while (lines >> key >> std::hex >> address)
            m_functions[NameKey(key)] = reinterpret_cast<void*>(address);
        spdlog::info("Papyrus natives from the previous load: {}", m_functions.size());
    }
}

void PapyrusService::HandOffFunctions() const noexcept
{
    std::ostringstream lines;
    for (const auto& [cKey, cpFunction] : m_functions)
        lines << cKey.c_str() << ' ' << std::hex << reinterpret_cast<uintptr_t>(cpFunction) << '\n';
    CoopReload::HandOff(kHandoffKey, lines.str());
}

const void* PapyrusService::Get(const String& acNamespace, const String& acFunction) const noexcept
{
    const auto itor = m_functions.find(NameKey(acNamespace + "::" + acFunction));
    if (itor != std::end(m_functions))
        return itor->second;

    return nullptr;
}

void PapyrusService::HandlePapyrusFunctionEvent(const PapyrusFunctionRegisterEvent& acEvent) noexcept
{
    m_functions[NameKey(acEvent.Namespace + "::" + acEvent.Name)] = acEvent.Function;
}
