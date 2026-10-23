#include <Papyrus/NativeReach.h>

#include <VersionDb.h>

#include <Zydis/Zydis.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NativeReach
{
namespace
{
// Calls followed from the native's function: deeper reaches unrelated engine code (papyrus_natives.py, depth 2-6).
constexpr int cDepth = 3;
// A function without unwind data (a leaf) is read until it returns, or this far.
constexpr uintptr_t cLeafBytes = 0x400;

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

struct Module
{
    uintptr_t Base;
    uintptr_t End;

    bool Contains(uintptr_t aAddress) const noexcept { return aAddress >= Base && aAddress < End; }
};

// What one function reads and calls.
struct Facts
{
    bool Reads{};
    std::vector<uintptr_t> Callees;
};

std::mutex s_lock;
std::unordered_map<BSScript::IFunction*, bool> s_natives;
std::unordered_map<uintptr_t, Facts> s_functions;

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

const ZydisDecoder& Decoder() noexcept
{
    static const ZydisDecoder s_decoder = [] {
        ZydisDecoder decoder;
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        return decoder;
    }();
    return s_decoder;
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

struct Range
{
    uintptr_t Begin;
    uintptr_t End;
};

// Reads the function at aFunction (in acModule), over each of its unwind entries: the per-player objects its operands
// address, and the functions it calls or tail-jumps to. In a plugin, an object or function is also reached through
// the cell holding its address.
Facts Read(uintptr_t aFunction, const Module& acModule, bool aThroughCells) noexcept
{
    Facts facts;
    DWORD64 imageBase = 0;
    const RUNTIME_FUNCTION* pEntry = RtlLookupFunctionEntry(aFunction, &imageBase, nullptr);
    const RUNTIME_FUNCTION* pRoot = pEntry ? Root(pEntry, imageBase) : nullptr;
    if (pRoot && imageBase + pRoot->BeginAddress != aFunction)
        return facts; // not a function's start
    // A leaf (no entry) is read until it returns.
    std::vector<Range> parts{{aFunction, pEntry ? imageBase + pEntry->EndAddress : std::min(aFunction + cLeafBytes, acModule.End)}};
    const auto cIsRead = [&parts](uintptr_t aAddress) {
        return std::ranges::any_of(parts, [aAddress](const Range& acPart) { return aAddress >= acPart.Begin && aAddress < acPart.End; });
    };
    // Another entry of this function: added to what is read.
    const auto cAddPart = [&](uintptr_t aAddress) {
        DWORD64 base = 0;
        const RUNTIME_FUNCTION* pPart = pRoot ? RtlLookupFunctionEntry(aAddress, &base, nullptr) : nullptr;
        if (!pPart || base != imageBase || Root(pPart, base)->BeginAddress != pRoot->BeginAddress)
            return false;
        if (!cIsRead(base + pPart->BeginAddress))
            parts.push_back({base + pPart->BeginAddress, base + pPart->EndAddress});
        return true;
    };

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    for (size_t part = 0; part < parts.size(); ++part)
    {
        const Range cPart = parts[part];
        for (uintptr_t at = cPart.Begin; at < cPart.End; at += instruction.length)
        {
            if (ZYAN_FAILED(ZydisDecoderDecodeFull(&Decoder(), reinterpret_cast<const void*>(at), cPart.End - at, &instruction, operands)))
                break;
            const bool cIsCall = instruction.meta.category == ZYDIS_CATEGORY_CALL || instruction.meta.category == ZYDIS_CATEGORY_UNCOND_BR;
            const bool cIsBranch = cIsCall || instruction.meta.category == ZYDIS_CATEGORY_COND_BR;
            for (uint8_t i = 0; i < instruction.operand_count_visible; ++i)
            {
                const ZydisDecodedOperand& acOperand = operands[i];
                const bool cIsMemory = acOperand.type == ZYDIS_OPERAND_TYPE_MEMORY && acOperand.mem.base == ZYDIS_REGISTER_RIP;
                const bool cIsTarget = acOperand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && acOperand.imm.is_relative && cIsBranch;
                ZyanU64 address = 0;
                if ((!cIsMemory && !cIsTarget) || ZYAN_FAILED(ZydisCalcAbsoluteAddress(&instruction, &acOperand, at, &address)))
                    continue;
                if (cIsTarget)
                {
                    if (!cIsRead(address) && !cAddPart(address) && cIsCall)
                        facts.Callees.push_back(address);
                }
                else if (PerPlayer().contains(address))
                    facts.Reads = true;
                else if (aThroughCells && acModule.Contains(address))
                {
                    const std::optional<uintptr_t> cHeld = HeldBy(address);
                    if (cHeld && PerPlayer().contains(*cHeld))
                        facts.Reads = true;
                    else if (cHeld && cIsCall && IsCode(*cHeld))
                        facts.Callees.push_back(*cHeld);
                }
            }
            const bool cEnds = instruction.mnemonic == ZYDIS_MNEMONIC_RET || instruction.mnemonic == ZYDIS_MNEMONIC_INT3 ||
                               instruction.mnemonic == ZYDIS_MNEMONIC_JMP;
            if (!pEntry && cEnds)
                break;
        }
        if (pEntry)
            cAddPart(cPart.End); // the next entry, when it is this function's
    }
    return facts;
}

bool Classify(uintptr_t aCallback) noexcept
{
    const Module& cGame = Game();
    const std::optional<Module> cOwn = ModuleOf(aCallback);
    if (!cOwn)
        return false;
    // The engine's code and the native's own plugin; never other modules (imports, the CRT).
    const auto cModuleOf = [&](uintptr_t aAddress) -> const Module* {
        return cGame.Contains(aAddress) ? &cGame : cOwn->Contains(aAddress) ? &*cOwn : nullptr;
    };

    std::unordered_set<uintptr_t> seen{aCallback};
    std::vector<uintptr_t> level{aCallback};
    for (int depth = 0; depth <= cDepth && !level.empty(); ++depth)
    {
        std::vector<uintptr_t> next;
        for (const uintptr_t cFunction : level)
        {
            auto it = s_functions.find(cFunction);
            if (it == s_functions.end())
            {
                const Module* pModule = cModuleOf(cFunction);
                it = s_functions.emplace(cFunction, pModule ? Read(cFunction, *pModule, pModule != &cGame) : Facts{}).first;
            }
            if (it->second.Reads)
                return true;
            for (const uintptr_t cCallee : it->second.Callees)
                if (cModuleOf(cCallee) && seen.insert(cCallee).second)
                    next.push_back(cCallee);
        }
        level = std::move(next);
    }
    return false;
}
} // namespace

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
