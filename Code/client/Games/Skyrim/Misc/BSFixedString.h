#pragma once

#include <Games/EngineNames.h>

struct BSFixedString
{
    BSFixedString();
    BSFixedString(const char* acpData);
    ~BSFixedString();

    void Set(const char* acpData);
    void Release() noexcept;

    operator const char*() const noexcept { return data; }

    [[nodiscard]] bool IsAscii() const noexcept { return true; }
    [[nodiscard]] const char* AsAscii() const noexcept { return data; }
    [[nodiscard]] const wchar_t* AsWide() const noexcept { return nullptr; }

    // Case-insensitive, as the pool is (EngineNames.h).
    [[nodiscard]] bool Is(std::string_view aName) const noexcept { return data && SameName(data, aName); }
    friend bool operator==(const BSFixedString& acName, const char* acpOther) noexcept
    {
        return acpOther ? acName.Is(acpOther) : acName.data == nullptr;
    }

    const char* data;
};
