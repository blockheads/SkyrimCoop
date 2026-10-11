#include <TiltedOnlinePCH.h>
#include "Handoff.h"

namespace CoopReload
{
static HMODULE Loader()
{
    return GetModuleHandleW(L"SkyrimCoopLoader.dll");
}

void HandOff(const char* acpKey, const std::string& acData)
{
    using TStore = void (*)(const char*, const char*, size_t);
    const HMODULE cLoader = Loader();
    if (const auto pStore = cLoader ? reinterpret_cast<TStore>(GetProcAddress(cLoader, "SkyrimCoopLoader_Store")) : nullptr)
        pStore(acpKey, acData.data(), acData.size());
}

std::optional<std::string> TakeHandoff(const char* acpKey)
{
    using TFetch = size_t (*)(const char*, char*, size_t);
    const HMODULE cLoader = Loader();
    const auto pFetch = cLoader ? reinterpret_cast<TFetch>(GetProcAddress(cLoader, "SkyrimCoopLoader_Fetch")) : nullptr;
    if (!pFetch)
        return std::nullopt;
    const size_t cSize = pFetch(acpKey, nullptr, 0);
    if (cSize == SIZE_MAX)
        return std::nullopt;
    std::string data(cSize, '\0');
    pFetch(acpKey, data.data(), data.size());
    return data;
}
} // namespace CoopReload
