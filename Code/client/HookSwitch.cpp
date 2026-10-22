#include "HookSwitch.h"

#include <Hooks/SkseBranchPatch.h>

#include <windows.h>

#include <string>
#include <string_view>

namespace
{
bool Disabled(const char* acpFile) noexcept
{
    char value[2048]{};
    if (GetEnvironmentVariableA("SKYRIMCOOP_DISABLE_HOOKS", value, sizeof(value)) == 0)
        return false;

    std::string_view name(acpFile);
    if (const auto cSlash = name.find_last_of("/\\"); cSlash != std::string_view::npos)
        name.remove_prefix(cSlash + 1);
    if (const auto cDot = name.find('.'); cDot != std::string_view::npos)
        name = name.substr(0, cDot);

    std::string_view list(value);
    while (!list.empty())
    {
        const auto cComma = list.find(',');
        if (list.substr(0, cComma) == name)
        {
            spdlog::warn("Hooks of {} left out (SKYRIMCOOP_DISABLE_HOOKS)", std::string(name));
            return true;
        }
        if (cComma == std::string_view::npos)
            break;
        list.remove_prefix(cComma + 1);
    }
    return false;
}
} // namespace

HookGate::HookGate(const char* acpFile) noexcept
    : m_suppressing(Disabled(acpFile))
{
    if (m_suppressing)
        Hooks::SuppressPatches(true);
}

HookGate::~HookGate() noexcept
{
    if (m_suppressing)
        Hooks::SuppressPatches(false);
}
