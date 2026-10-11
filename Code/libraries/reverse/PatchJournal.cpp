#include <windows.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

// Every byte our code patches into the game, with what was there before, so that unloading the
// plugin (hot reload) can put the game back the way it found it.
namespace TiltedPhoques
{
namespace
{
struct Patch
{
    uint8_t* Address;
    std::vector<uint8_t> Original;
};

std::mutex s_lock;
std::vector<Patch> s_patches;
std::vector<void (*)()> s_unloadCallbacks;
bool s_suppressed = false;
} // namespace

void SuppressPatches(bool aSuppress) noexcept
{
    s_suppressed = aSuppress;
}

bool PatchesSuppressed() noexcept
{
    return s_suppressed;
}

void AtUnload(void (*apCallback)()) noexcept
{
    std::scoped_lock _(s_lock);
    s_unloadCallbacks.push_back(apCallback);
}

void RunUnloadCallbacks() noexcept
{
    std::vector<void (*)()> callbacks;
    {
        std::scoped_lock _(s_lock);
        callbacks.swap(s_unloadCallbacks);
    }
    for (auto it = callbacks.rbegin(); it != callbacks.rend(); ++it)
        (*it)();
}

void RecordPatch(void* apAddress, size_t aSize) noexcept
{
    std::scoped_lock _(s_lock);
    auto* pBytes = static_cast<uint8_t*>(apAddress);
    s_patches.push_back({pBytes, std::vector<uint8_t>(pBytes, pBytes + aSize)});
}

void RestorePatches() noexcept
{
    std::scoped_lock _(s_lock);
    // Newest first: a later patch over an earlier one recorded the earlier one's bytes.
    for (auto it = s_patches.rbegin(); it != s_patches.rend(); ++it)
    {
        DWORD previous = 0;
        if (!VirtualProtect(it->Address, it->Original.size(), PAGE_EXECUTE_READWRITE, &previous))
            continue;
        std::memcpy(it->Address, it->Original.data(), it->Original.size());
        FlushInstructionCache(GetCurrentProcess(), it->Address, it->Original.size());
        VirtualProtect(it->Address, it->Original.size(), previous, &previous);
    }
    s_patches.clear();
}
} // namespace TiltedPhoques
