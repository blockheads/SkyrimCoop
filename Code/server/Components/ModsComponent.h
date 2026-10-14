#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

namespace ESLoader
{
struct PluginData;
}

namespace Server
{

struct ModsComponent
{
    struct Entry
    {
        uint32_t id;
        uint32_t refCount;
    };

    void AddServerMod(const ESLoader::PluginData& acData);

    const auto& GetServerMods() const noexcept { return m_serverMods; }

    bool IsInstalled(const String& acpFileName) const noexcept;

    using TModList = TiltedPhoques::Map<String, Entry>;

private:
    // List of mods installed on the server.
    TModList m_serverMods;
};

} // namespace Server
