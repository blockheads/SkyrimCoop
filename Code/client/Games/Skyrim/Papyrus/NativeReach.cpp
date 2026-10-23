#include <Papyrus/NativeReach.h>

#include <VersionDb.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NativeReach
{
namespace
{
using Module = Range; // a module's image: its base to its end

// A CommonLibSSE native keeps a std::function at +0x50 (NativeFunction::_stub): MSVC holds a function pointer in
// place (its _Func_impl: vtable, then the pointer) with the _Impl pointer last, pointing at itself.
struct StdFunction
{
    void* pImplVtable;     // 00
    void* pCallee;         // 08
    uint8_t storage[0x28]; // 10
    void* pImpl;           // 38
};
static_assert(sizeof(StdFunction) == sizeof(std::function<void()>) && offsetof(StdFunction, pImpl) == 0x38);
static_assert(offsetof(BSScript::NativeFunction, pFunction) == 0x50);

std::mutex s_lock;
std::unordered_map<BSScript::IFunction*, bool> s_natives;
std::unordered_map<uintptr_t, Facts> s_functions;

std::shared_mutex s_callsLock; // a leaf; CallOf reads it on every native call
std::unordered_map<BSScript::IFunction*, Call> s_calls;

// This game's per-player objects: the player, its stats and its screen.
const std::unordered_set<uintptr_t>& PerPlayer() noexcept
{
    POINTER_SKYRIMSE(void, s_thePlayer, 403521);
    POINTER_SKYRIMSE(void, s_playerCharacter, 401069); // the one PlayerCharacter::Get reads
    POINTER_SKYRIMSE(void, s_miscStats, 400662);
    POINTER_SKYRIMSE(void, s_ui, 400327);
    POINTER_SKYRIMSE(void, s_uiMessageQueue, 400445);
    POINTER_SKYRIMSE(void, s_playerControls, 400864);
    POINTER_SKYRIMSE(void, s_controlMap, 400863);
    POINTER_SKYRIMSE(void, s_menuControls, 401263);
    POINTER_SKYRIMSE(void, s_playerCamera, 400802);
    POINTER_SKYRIMSE(void, s_imageSpaceManager, 414660);
    POINTER_SKYRIMSE(void, s_inputDeviceManager, 402776);
    POINTER_SKYRIMSE(void, s_screenSplatter, 400672);
    POINTER_SKYRIMSE(void, s_rumbleLeft, 410222); // the gamepad's rumble queues, read by BSInputDeviceManager::PollInputDevices
    POINTER_SKYRIMSE(void, s_rumbleRight, 410225);
    static const std::unordered_set<uintptr_t> s_addresses = [] {
        std::unordered_set<uintptr_t> addresses;
        for (const auto* pObject : {&s_thePlayer, &s_playerCharacter, &s_miscStats, &s_ui, &s_uiMessageQueue, &s_playerControls, &s_controlMap,
                                    &s_menuControls, &s_playerCamera, &s_imageSpaceManager, &s_inputDeviceManager, &s_screenSplatter,
                                    &s_rumbleLeft, &s_rumbleRight})
            if (void* pAddress = pObject->GetPtr())
                addresses.insert(reinterpret_cast<uintptr_t>(pAddress));
        return addresses;
    }();
    return s_addresses;
}

// The engine functions a script's call ends in: a cutscene's (docs/design/cutscenes.md), and the world's.
const std::unordered_map<uintptr_t, Call>& CallFunctions() noexcept
{
    POINTER_SKYRIMSE(void, s_toggleControls, 68545);   // ControlMap::ToggleControls
    POINTER_SKYRIMSE(void, s_fade, 52847);             // the fade FadeOutGame and the menus use
    POINTER_SKYRIMSE(void, s_imodTrigger, 18570);      // ImageSpaceModifierInstanceForm::Trigger
    POINTER_SKYRIMSE(void, s_imodCrossFade, 18575);    // TESImageSpaceModifier::ApplyCrossFade
    POINTER_SKYRIMSE(void, s_imodStop, 18573);         // ImageSpaceModifierInstanceForm::Stop
    POINTER_SKYRIMSE(void, s_imodStopCrossFade, 18577);
    POINTER_SKYRIMSE(void, s_shakeCamera, 33012);
    POINTER_SKYRIMSE(void, s_forceFirstPerson, 50790); // PlayerCamera
    POINTER_SKYRIMSE(void, s_forceThirdPerson, 50796);
    POINTER_SKYRIMSE(void, s_setCameraTarget, 50838);
    POINTER_SKYRIMSE(void, s_musicAdd, 35145);         // BSMusicManager
    POINTER_SKYRIMSE(void, s_musicRemove, 35146);
    POINTER_SKYRIMSE(void, s_rumble, 68528);           // the gamepad's rumble queues
    POINTER_SKYRIMSE(void, s_setStage, 25004);         // TESQuest::SetStage
    static const std::unordered_map<uintptr_t, Call> s_functions = [] {
        std::unordered_map<uintptr_t, Call> functions;
        functions[reinterpret_cast<uintptr_t>(s_toggleControls.GetPtr())] = Call::kControls;
        functions[reinterpret_cast<uintptr_t>(s_setStage.GetPtr())] = Call::kWorld;
        for (const auto* pFunction : {&s_fade, &s_imodTrigger, &s_imodCrossFade, &s_imodStop, &s_imodStopCrossFade, &s_shakeCamera, &s_forceFirstPerson,
                                      &s_forceThirdPerson, &s_setCameraTarget, &s_musicAdd, &s_musicRemove, &s_rumble})
            functions[reinterpret_cast<uintptr_t>(pFunction->GetPtr())] = Call::kScreen;
        functions.erase(0);
        return functions;
    }();
    return s_functions;
}

// The screen's objects read where no function says it: the movie player (Game.PlayBink).
const std::unordered_set<uintptr_t>& ScreenObjects() noexcept
{
    POINTER_SKYRIMSE(void, s_moviePlayer, 412214);
    static const std::unordered_set<uintptr_t> s_objects{reinterpret_cast<uintptr_t>(s_moviePlayer.GetPtr())};
    return s_objects;
}

std::optional<Module> ModuleOf(uintptr_t aAddress) noexcept
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(aAddress), &module))
        return std::nullopt;
    const auto cBase = reinterpret_cast<uintptr_t>(module);
    const auto* pHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(cBase + reinterpret_cast<const IMAGE_DOS_HEADER*>(cBase)->e_lfanew);
    return Module{cBase, cBase + pHeaders->OptionalHeader.SizeOfImage};
}

const Module& Game() noexcept
{
    static const Module s_game = ModuleOf(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))).value_or(Module{});
    return s_game;
}

DWORD Protection(uintptr_t aAddress) noexcept
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(aAddress), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return PAGE_NOACCESS;
    return info.Protect;
}

bool IsCode(uintptr_t aAddress) noexcept
{
    return Protection(aAddress) & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
}

// The pointer a data cell holds (a plugin's REL::Relocation of an engine object or function).
std::optional<uintptr_t> HeldBy(uintptr_t aCell) noexcept
{
    if (!(Protection(aCell) & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY)))
        return std::nullopt;
    return *reinterpret_cast<const uintptr_t*>(aCell);
}

// The native's C++ function: the engine's (and SKSE's) pointer at +0x50, or a CommonLibSSE std::function's.
uintptr_t CallbackOf(BSScript::IFunction* apFunction) noexcept
{
    if (!apFunction->GetIsNative())
        return 0;
    auto* pNative = static_cast<BSScript::NativeFunction*>(apFunction);
    if (const auto cRaw = reinterpret_cast<uintptr_t>(pNative->pFunction); IsCode(cRaw))
        return cRaw;
    if (Game().Contains(*reinterpret_cast<const uintptr_t*>(pNative)))
        return 0; // an engine native without a function
    const auto* pStub = reinterpret_cast<const StdFunction*>(&pNative->pFunction);
    const auto cCallee = reinterpret_cast<uintptr_t>(pStub->pCallee);
    return pStub->pImpl == pStub && IsCode(cCallee) ? cCallee : 0;
}

// UNWIND_INFO's head (the x64 exception ABI): a chained entry's (UNW_FLAG_CHAININFO) RUNTIME_FUNCTION follows its
// codes, padded to an even count.
struct UnwindInfo
{
    uint8_t versionAndFlags;
    uint8_t prologueSize;
    uint8_t codeCount;
    uint8_t frame;
    uint16_t codes[1];
};

// The entry a function's code starts with: MSVC splits a body over several entries chained to its first.
const RUNTIME_FUNCTION* Root(const RUNTIME_FUNCTION* apEntry, uintptr_t aImageBase) noexcept
{
    for (;;)
    {
        const auto* pInfo = reinterpret_cast<const UnwindInfo*>(aImageBase + apEntry->UnwindData);
        if (!((pInfo->versionAndFlags >> 3) & UNW_FLAG_CHAININFO))
            return apEntry;
        apEntry = reinterpret_cast<const RUNTIME_FUNCTION*>(&pInfo->codes[(pInfo->codeCount + 1) & ~1]);
    }
}

// This process's code, as CodeReach reads it.
class ProcessCode final : public Code
{
  public:
    std::span<const uint8_t> At(uintptr_t aAt, size_t aMax) const noexcept override
    {
        return {reinterpret_cast<const uint8_t*>(aAt), aMax};
    }

    std::optional<Entry> EntryOf(uintptr_t aAddress) const noexcept override
    {
        DWORD64 imageBase = 0;
        const RUNTIME_FUNCTION* pEntry = RtlLookupFunctionEntry(aAddress, &imageBase, nullptr);
        if (!pEntry)
            return std::nullopt;
        return Entry{{imageBase + pEntry->BeginAddress, imageBase + pEntry->EndAddress}, imageBase + Root(pEntry, imageBase)->BeginAddress};
    }

    std::optional<uintptr_t> HeldBy(uintptr_t aCell) const noexcept override { return NativeReach::HeldBy(aCell); }
    bool IsCode(uintptr_t aAddress) const noexcept override { return NativeReach::IsCode(aAddress); }
};

const Objects& EngineObjects() noexcept
{
    static const Objects s_objects{PerPlayer(), ScreenObjects()};
    return s_objects;
}

// The facts of a function, read once (under s_lock).
const Facts& FactsOf(uintptr_t aFunction, const Module& acGame, const Module& acOwn) noexcept
{
    auto it = s_functions.find(aFunction);
    if (it == s_functions.end())
    {
        const Module* pModule = acGame.Contains(aFunction) ? &acGame : acOwn.Contains(aFunction) ? &acOwn : nullptr;
        static const ProcessCode s_code;
        it = s_functions.emplace(aFunction, pModule ? Read(aFunction, *pModule, pModule != &acGame, s_code, EngineObjects()) : Facts{}).first;
    }
    return it->second;
}

Call ClassifyCallback(uintptr_t aCallback) noexcept
{
    const std::optional<Module> cOwn = ModuleOf(aCallback);
    return cOwn ? ClassifyCall(FactsOf(aCallback, Game(), *cOwn), CallFunctions()) : Call::kNone;
}

// The engine's code and the native's own plugin are followed; never other modules (imports, the CRT).
bool Classify(uintptr_t aCallback) noexcept
{
    const Module& cGame = Game();
    const std::optional<Module> cOwn = ModuleOf(aCallback);
    if (!cOwn)
        return false;
    return Reaches(
        aCallback, [&](uintptr_t aFunction) -> const Facts& { return FactsOf(aFunction, cGame, *cOwn); },
        [&](uintptr_t aCallee) { return cGame.Contains(aCallee) || cOwn->Contains(aCallee); });
}
} // namespace

Call CallOf(BSScript::IFunction* apFunction) noexcept
{
    {
        std::shared_lock _{s_callsLock};
        if (const auto it = s_calls.find(apFunction); it != s_calls.end())
            return it->second;
    }
    Call call = Call::kNone;
    {
        std::scoped_lock _{s_lock};
        if (const uintptr_t cCallback = CallbackOf(apFunction))
            call = ClassifyCallback(cCallback);
    }
    if (call != Call::kNone)
        spdlog::debug("NativeReach: {}.{} calls the {}", apFunction->GetObjectTypeName().AsAscii(), apFunction->GetName().AsAscii(),
                      call == Call::kWorld ? "world" : call == Call::kControls ? "controls" : "screen");
    std::unique_lock _{s_callsLock};
    s_calls.emplace(apFunction, call);
    return call;
}

bool ReachesThisPlayer(BSScript::IFunction* apFunction) noexcept
{
    std::scoped_lock _{s_lock};
    if (const auto it = s_natives.find(apFunction); it != s_natives.end())
        return it->second;
    const uintptr_t cCallback = CallbackOf(apFunction);
    const bool cReaches = cCallback && Classify(cCallback);
    s_natives.emplace(apFunction, cReaches);
    spdlog::debug("NativeReach: {}.{} {}", apFunction->GetObjectTypeName().AsAscii(), apFunction->GetName().AsAscii(),
                  !cCallback ? "has no function we can read" : cReaches ? "acts on this game's player" : "does not");
    return cReaches;
}
} // namespace NativeReach
