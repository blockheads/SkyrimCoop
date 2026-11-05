#pragma once

#include <cstdint>
#include <unordered_map>

// Which guest did a crime the engine must name on the host's player (docs/re/systems/crime.md, After the call).
struct CrimeCulprits
{
    // A crime record's address is reused once freed, so a host crime (no culprit) forgets what was there.
    void Remember(const void* acpCrime, uint32_t aCulprit) noexcept
    {
        if (aCulprit)
            m_culprits[acpCrime] = aCulprit;
        else
            m_culprits.erase(acpCrime);
    }

    [[nodiscard]] uint32_t Of(const void* acpCrime) const noexcept
    {
        const auto cFound = m_culprits.find(acpCrime);
        return cFound == m_culprits.end() ? 0 : cFound->second;
    }

    void Clear() noexcept
    {
        m_culprits.clear();
    }

private:
    std::unordered_map<const void*, uint32_t> m_culprits;
};
