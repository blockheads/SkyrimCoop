#pragma once

#include <core/EngineNames.h>

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

// A pooled string's entry: BSFixedString::data points at its text (BSStringPool::LookupOrInsert 69201).
struct BSStringPoolEntry
{
    BSStringPoolEntry* pNext;
    uint32_t uiFlags; // the reference count in the low 15 bits; 0x8000 a released entry
    uint32_t uiLength;
    uint64_t uiUnk10;

    [[nodiscard]] static const BSStringPoolEntry* Of(const char* acpText) noexcept
    {
        return acpText ? reinterpret_cast<const BSStringPoolEntry*>(acpText - sizeof(BSStringPoolEntry)) : nullptr;
    }
};

static_assert(sizeof(BSStringPoolEntry) == 0x18);
static_assert(offsetof(BSStringPoolEntry, uiFlags) == 0x8);
