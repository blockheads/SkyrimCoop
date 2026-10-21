#include <Games/Rtti.h>

#include <windows.h>

#include <cstring>

namespace Rtti
{
namespace
{
struct Image
{
    uintptr_t Base;
    uintptr_t End;

    // Written so that no sum can wrap around (a null vtable minus 8 is near the top of memory).
    bool Holds(uintptr_t aAddress, size_t aSize) const noexcept { return aAddress >= Base && aAddress < End && aSize <= End - aAddress; }
    uintptr_t At(uint32_t aRva) const noexcept { return Base + aRva; }
};

const Image& Game() noexcept
{
    static const Image s_image = []
    {
        const auto cBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* pHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(cBase + reinterpret_cast<const IMAGE_DOS_HEADER*>(cBase)->e_lfanew);
        return Image{cBase, cBase + pHeaders->OptionalHeader.SizeOfImage};
    }();
    return s_image;
}

// The CompleteObjectLocator's {signature, offset, cdOffset, type descriptor, class hierarchy} RVAs, or null.
const uint32_t* Locator(const void* apObject) noexcept
{
    const Image& cImage = Game();
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(apObject, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
        reinterpret_cast<uintptr_t>(apObject) + 0x18 > reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize)
        return nullptr;
    const auto cVTable = *static_cast<const uintptr_t*>(apObject);
    if (!cImage.Holds(cVTable - 8, 8))
        return nullptr;
    const auto cLocator = *reinterpret_cast<const uintptr_t*>(cVTable - 8);
    if (!cImage.Holds(cLocator, 24))
        return nullptr;
    const auto* pLocator = reinterpret_cast<const uint32_t*>(cLocator);
    if (pLocator[0] != 1 || !cImage.Holds(cImage.At(pLocator[3]) + 16, 64) || !cImage.Holds(cImage.At(pLocator[4]), 16))
        return nullptr;
    return pLocator;
}
} // namespace

bool ClassName(const void* apObject, char* apOut, size_t aSize) noexcept
{
    apOut[0] = '\0';
    const uint32_t* pLocator = Locator(apObject);
    if (!pLocator)
        return false;
    // ".?AVActor@@" -> "Actor"
    const char* pStart = reinterpret_cast<const char*>(Game().At(pLocator[3]) + 16 + 4);
    size_t length = 0;
    while (length < 40 && pStart[length] && pStart[length] != '@')
        ++length;
    const size_t cCopied = length < aSize - 1 ? length : aSize - 1;
    memcpy(apOut, pStart, cCopied);
    apOut[cCopied] = '\0';
    return true;
}

bool IsForm(const void* apObject) noexcept
{
    const uint32_t* pLocator = Locator(apObject);
    if (!pLocator)
        return false;
    const Image& cImage = Game();
    const auto* pHierarchy = reinterpret_cast<const uint32_t*>(cImage.At(pLocator[4]));
    if (!cImage.Holds(cImage.At(pHierarchy[3]), pHierarchy[2] * 4))
        return false;
    const auto* pBases = reinterpret_cast<const uint32_t*>(cImage.At(pHierarchy[3]));
    for (uint32_t i = 0; i < pHierarchy[2] && i < 64; ++i)
    {
        if (!cImage.Holds(cImage.At(pBases[i]), 4))
            continue;
        const uintptr_t cName = cImage.At(*reinterpret_cast<const uint32_t*>(cImage.At(pBases[i]))) + 16;
        if (cImage.Holds(cName, 14) && memcmp(reinterpret_cast<const char*>(cName), ".?AVTESForm@@", 14) == 0)
            return true;
    }
    return false;
}
} // namespace Rtti
