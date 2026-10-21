#include <TiltedOnlinePCH.h>
#include "InputTrace.h"
#include "EngineCalls.h"

#include <Games/Rtti.h>
#include <Games/Skyrim/BSInput/InputEvent.h>

#include <chrono>

namespace TestHarness
{
namespace
{
using TProcessEvent = uint32_t (*)(void* apSink, InputEvent* const* appChain, void* apSource);

// One sink class's ProcessEvent slot (vtable slot 1), wrapped; vtables are per class, so one wrap covers its sinks.
struct Wrapped
{
    void** pSlot;
    TProcessEvent Original;
    std::string Name;
};

struct Seen
{
    const ButtonEvent* pEvent;
    std::string Line;
    std::string Sinks;
};

std::vector<Wrapped> s_wrapped;
std::vector<Seen> s_seen; // this frame's, on the main thread
std::chrono::steady_clock::time_point s_nextScan{};

constexpr const char* cItemMenus[] = {"ContainerMenu", "InventoryMenu", "BarterMenu", "GiftMenu"};

const char* DeviceName(uint32_t aDevice) noexcept
{
    switch (aDevice)
    {
    case InputEvent::kKeyboard: return "keyboard";
    case InputEvent::kMouse: return "mouse";
    case InputEvent::kGamepad: return "gamepad";
    default: return "device";
    }
}

// The press as the game has it now: its key, user event, the menus open (top last) and an item menu's lists.
std::string Describe(const ButtonEvent& acEvent)
{
    const char* pUserEvent = acEvent.userEvent.AsAscii();
    std::string line = fmt::format("Input: {} 0x{:X} '{}' {}", DeviceName(acEvent.header.device), acEvent.idCode, pUserEvent ? pUserEvent : "",
                                   acEvent.IsPress() ? "press" : "release");
    const auto cMenus = EngineCalls::GetOpenMenus();
    std::string menus;
    for (const auto& cMenu : cMenus)
        if (cMenu != "HUD Menu" && cMenu != "Cursor Menu")
            menus += (menus.empty() ? "" : ", ") + cMenu;
    line += "; menus " + (menus.empty() ? std::string("none") : menus);
    // InventoryLists.as: 1 one panel, 2 two panels; the others slide them, and keys are dropped meanwhile.
    for (const char* pMenu : cItemMenus)
        if (const auto cState = EngineCalls::GetMenuVariable(pMenu, "_root.Menu_mc.InventoryLists_mc.currentState"))
            line += fmt::format(" ({} lists {})", pMenu, *cState);
    return line;
}

uint32_t WrappedProcessEvent(void* apSink, InputEvent* const* appChain, void* apSource)
{
    void** pVTable = *static_cast<void***>(apSink);
    const auto cWrap = std::ranges::find(s_wrapped, &pVTable[1], &Wrapped::pSlot);
    if (cWrap == s_wrapped.end())
        return 0; // not ours to have wrapped: cannot happen, the slot points here only through s_wrapped
    const uint32_t cResult = cWrap->Original(apSink, appChain, apSource);
    for (const InputEvent* pEvent = appChain ? *appChain : nullptr; pEvent; pEvent = pEvent->pNext)
    {
        if (pEvent->eventType != InputEvent::kButton)
            continue;
        const auto* pButton = reinterpret_cast<const ButtonEvent*>(pEvent);
        if (!pButton->IsPress() && !pButton->IsRelease())
            continue; // held: sent every frame
        auto seen = std::ranges::find(s_seen, pButton, &Seen::pEvent);
        if (seen == s_seen.end())
            seen = s_seen.insert(s_seen.end(), {pButton, Describe(*pButton), {}});
        seen->Sinks += fmt::format("{}{} {}", seen->Sinks.empty() ? "" : ", ", cWrap->Name, cResult ? "stop" : "continue");
    }
    return cResult;
}

void Wrap(void* apSink) noexcept
{
    void** pVTable = *static_cast<void***>(apSink);
    void** pSlot = &pVTable[1];
    if (*pSlot == reinterpret_cast<void*>(&WrappedProcessEvent) || std::ranges::find(s_wrapped, pSlot, &Wrapped::pSlot) != s_wrapped.end())
        return;
    // The game's classes by name; another module's (SKSE, a plugin) by the module.
    char name[64];
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH]{};
    if (!Rtti::ClassName(apSink, name, sizeof(name)))
    {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(pVTable), &module);
        GetModuleFileNameW(module, path, MAX_PATH);
        snprintf(name, sizeof(name), "a sink of %ls", module ? std::filesystem::path(path).filename().c_str() : L"?");
    }
    DWORD protection = 0;
    if (!VirtualProtect(pSlot, sizeof(void*), PAGE_READWRITE, &protection))
        return;
    s_wrapped.push_back({pSlot, reinterpret_cast<TProcessEvent>(*pSlot), name});
    *pSlot = reinterpret_cast<void*>(&WrappedProcessEvent);
    VirtualProtect(pSlot, sizeof(void*), protection, &protection);
    spdlog::info("InputTrace: watching {}", name);
}
} // namespace

void InputTrace::Poll() noexcept
{
    for (const auto& cSeen : s_seen)
        spdlog::info("{}; {}", cSeen.Line, cSeen.Sinks);
    s_seen.clear();

    // Sinks register at start-up and with some menus: a look once a second.
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < s_nextScan)
        return;
    s_nextScan = cNow + std::chrono::seconds(1);
    VersionDbPtr<InputEventSource*> source(402776); // BSInputDeviceManager: the event source at its start
    const InputEventSource* pSource = *source.Get();
    if (!pSource)
        return;
    for (uint32_t i = 0; i < pSource->sinks.length; ++i)
        if (pSource->sinks.data[i])
            Wrap(pSource->sinks.data[i]);
}

void InputTrace::Stop() noexcept
{
    for (const auto& cWrap : s_wrapped)
    {
        DWORD protection = 0;
        if (VirtualProtect(cWrap.pSlot, sizeof(void*), PAGE_READWRITE, &protection))
        {
            *cWrap.pSlot = reinterpret_cast<void*>(cWrap.Original);
            VirtualProtect(cWrap.pSlot, sizeof(void*), protection, &protection);
        }
    }
    s_wrapped.clear();
    s_seen.clear();
}
} // namespace TestHarness
