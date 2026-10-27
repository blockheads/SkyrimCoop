
#include <MinHook.h>

#include <cstring>
#include <initializer_list>

#include <Hooks/FunctionHook.h>
#include <Hooks/Memory.h>

#define RtlOffsetToPointer(Base, Offset) ((PCHAR)(((PCHAR)(Base)) + ((ULONG_PTR)(Offset))))

namespace Hooks
{
    static void** GetImportedFunction(const wchar_t *acpModuleName, const char* acpLibraryName, const char* acpMethod) noexcept;

    namespace
    {
        HookObserver s_observer = nullptr;
        HookObserverFilter s_observerFilter = nullptr;
        // Executable pages holding the observer stubs; freed when every hook is removed.
        std::vector<uint8_t*> s_stubPages;
        size_t s_stubPageUsed = 0;
        constexpr size_t cStubPageSize = 4096;
        constexpr size_t cStubSize = 128;

        // A stub that calls the observer with the hook's name and first argument, keeping every
        // argument register (rcx, rdx, r8, r9, xmm0-3) and the stack as they were, then jumps to
        // the hook (or, with appTargetSlot, to wherever the slot points when called: a probe's
        // trampoline, which MinHook writes after the stub exists). The hook sees the call exactly
        // as the game made it.
        void* BuildStub(void* apHook, void** appTargetSlot, const char* acpName) noexcept
        {
            if (s_stubPages.empty() || s_stubPageUsed + cStubSize > cStubPageSize)
            {
                auto* pPage = static_cast<uint8_t*>(VirtualAlloc(nullptr, cStubPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
                if (!pPage)
                    return apHook;
                s_stubPages.push_back(pPage);
                s_stubPageUsed = 0;
            }
            uint8_t* pStub = s_stubPages.back() + s_stubPageUsed;
            s_stubPageUsed += cStubSize;

            size_t at = 0;
            auto emit = [&](std::initializer_list<uint8_t> aBytes) { for (const auto cByte : aBytes) pStub[at++] = cByte; };
            auto emit64 = [&](const void* apValue) { std::memcpy(pStub + at, &apValue, 8); at += 8; };

            // On entry rsp = 8 (mod 16); 4 pushes and 0x68 bring it to 0 for the call.
            emit({0x51, 0x52, 0x41, 0x50, 0x41, 0x51});             // push rcx, rdx, r8, r9
            emit({0x48, 0x83, 0xEC, 0x68});                         // sub rsp, 0x68
            emit({0x0F, 0x11, 0x44, 0x24, 0x20});                   // movups [rsp+0x20], xmm0
            emit({0x0F, 0x11, 0x4C, 0x24, 0x30});                   // movups [rsp+0x30], xmm1
            emit({0x0F, 0x11, 0x54, 0x24, 0x40});                   // movups [rsp+0x40], xmm2
            emit({0x0F, 0x11, 0x5C, 0x24, 0x50});                   // movups [rsp+0x50], xmm3
            emit({0x48, 0x89, 0xCA});                               // mov rdx, rcx (first argument)
            emit({0x48, 0xB9}); emit64(acpName);                    // mov rcx, name
            emit({0x48, 0xB8}); emit64(reinterpret_cast<void*>(s_observer)); // mov rax, observer
            emit({0xFF, 0xD0});                                     // call rax
            emit({0x0F, 0x10, 0x44, 0x24, 0x20});                   // movups xmm0, [rsp+0x20]
            emit({0x0F, 0x10, 0x4C, 0x24, 0x30});                   // movups xmm1, [rsp+0x30]
            emit({0x0F, 0x10, 0x54, 0x24, 0x40});                   // movups xmm2, [rsp+0x40]
            emit({0x0F, 0x10, 0x5C, 0x24, 0x50});                   // movups xmm3, [rsp+0x50]
            emit({0x48, 0x83, 0xC4, 0x68});                         // add rsp, 0x68
            emit({0x41, 0x59, 0x41, 0x58, 0x5A, 0x59});             // pop r9, r8, rdx, rcx
            if (appTargetSlot)
            {
                emit({0x48, 0xB8}); emit64(appTargetSlot);          // mov rax, slot
                emit({0x48, 0x8B, 0x00});                           // mov rax, [rax]
            }
            else
            {
                emit({0x48, 0xB8}); emit64(apHook);                 // mov rax, hook
            }
            emit({0xFF, 0xE0});                                     // jmp rax
            FlushInstructionCache(GetCurrentProcess(), pStub, at);
            return pStub;
        }

        void* ObservedHook(void* apHook, const char* acpName) noexcept
        {
            if (!s_observer || !acpName || (s_observerFilter && !s_observerFilter(acpName)))
                return apHook;
            return BuildStub(apHook, nullptr, acpName);
        }

        void FreeStubs() noexcept
        {
            for (auto* pPage : s_stubPages)
                VirtualFree(pPage, 0, MEM_RELEASE);
            s_stubPages.clear();
            s_stubPageUsed = 0;
        }
    }

    void SetHookObserver(HookObserver aObserver, HookObserverFilter aFilter) noexcept
    {
        s_observer = aObserver;
        s_observerFilter = aFilter;
    }

    FunctionHook::FunctionHook() noexcept
        : m_ppDetourFunction(nullptr)
        , m_pSystemFunction(nullptr)
        , m_pHookFunction(nullptr)
        , m_pName(nullptr)
    {
    }

    FunctionHook::FunctionHook(void** appSystemFunction, void* apHookFunction, const char* acpName) noexcept
        : m_ppDetourFunction(appSystemFunction)
        , m_pSystemFunction(*appSystemFunction)
        , m_pHookFunction(ObservedHook(apHookFunction, acpName))
        , m_pName(acpName)
    {
    }

    FunctionHook::~FunctionHook() noexcept
    {
        if (m_ppDetourFunction != nullptr)
        {
            MH_DisableHook(m_pSystemFunction);
            MH_RemoveHook(m_pSystemFunction);
        }
    }

    FunctionHook::FunctionHook(FunctionHook&& aRhs) noexcept
        : FunctionHook()
    {
        this->operator=(std::move(aRhs));
    }

    FunctionHook& FunctionHook::operator=(FunctionHook&& aRhs) noexcept
    {
        std::swap(m_ppDetourFunction, aRhs.m_ppDetourFunction);
        std::swap(m_pSystemFunction, aRhs.m_pSystemFunction);
        std::swap(m_pHookFunction, aRhs.m_pHookFunction);
        std::swap(m_pName, aRhs.m_pName);

        return *this;
    }

    FunctionHookManager::FunctionHookManager() noexcept
    {
        MH_Initialize();
    }

    FunctionHookManager::~FunctionHookManager() noexcept
    {
        UninstallHooks();

        MH_Uninitialize();
    }

    void FunctionHookManager::InstallDelayedHooks() noexcept
    {
        for (auto& hook : m_delayedHooks)
        {
            MH_CreateHook(hook.m_pSystemFunction, hook.m_pHookFunction, hook.m_ppDetourFunction);
            MH_EnableHook(hook.m_pSystemFunction);
            m_installedHooks.emplace_back(std::move(hook));
        }

        m_delayedHooks.clear();
    }

    void FunctionHookManager::UninstallHooks() noexcept
    {
        for (size_t i = 0; i < m_installedHooks.size(); ++i)
        {
            m_installedHooks[i].m_ppDetourFunction = nullptr;
        }

        //Mhook_UnhookEx(pHooks, m_installedHooks.size());

        m_installedHooks.clear();

        for (auto& iatHook : m_iatHooks)
        {
            const vp::ScopedContext thunkMemory(iatHook.pThunk, sizeof(iatHook.pThunk));
            thunkMemory.Write(iatHook.pOriginal);
        }
    }

    void FunctionHookManager::RemoveAllHooks() noexcept
    {
        MH_DisableHook(MH_ALL_HOOKS);
        for (auto& hook : m_installedHooks)
        {
            MH_RemoveHook(hook.m_pSystemFunction);
            hook.m_ppDetourFunction = nullptr;
        }
        m_installedHooks.clear();
        m_delayedHooks.clear();

        for (auto& iatHook : m_iatHooks)
        {
            const vp::ScopedContext thunkMemory(iatHook.pThunk, sizeof(iatHook.pThunk));
            thunkMemory.Write(iatHook.pOriginal);
        }
        m_iatHooks.clear();

        // Also hooks made with MinHook directly; frees the trampoline blocks it reserved next to
        // the game, which repeated reloads would otherwise use up.
        MH_Uninitialize();
        FreeStubs();
    }

    bool FunctionHookManager::AddProbe(void* apFunction, const char* acpName) noexcept
    {
        if (!s_observer || PatchesSuppressed())
            return false;
        auto** ppTrampoline = new void*{nullptr}; // read by the stub on every call: never freed
        void* pStub = BuildStub(nullptr, ppTrampoline, acpName);
        if (!pStub || MH_CreateHook(apFunction, pStub, ppTrampoline) != MH_OK || MH_EnableHook(apFunction) != MH_OK)
            return false;
        FunctionHook hook;
        hook.m_ppDetourFunction = ppTrampoline;
        hook.m_pSystemFunction = apFunction;
        hook.m_pHookFunction = pStub;
        hook.m_pName = acpName;
        m_installedHooks.emplace_back(std::move(hook));
        return true;
    }

    void FunctionHookManager::Add(FunctionHook aFunctionHook, const bool aDelayed) noexcept
    {
        if (PatchesSuppressed())
            return;
        if (aDelayed)
            m_delayedHooks.emplace_back(std::move(aFunctionHook));
        else if (MH_CreateHook(aFunctionHook.m_pSystemFunction, aFunctionHook.m_pHookFunction, aFunctionHook.m_ppDetourFunction) == MH_OK &&
                 MH_EnableHook(aFunctionHook.m_pSystemFunction) == MH_OK)
            m_installedHooks.emplace_back(std::move(aFunctionHook));
    }

    bool FunctionHookManager::AddChained(FunctionHook aHook) noexcept
    {
        if (!PatchesSuppressed())
        {
            for (const auto& installed : m_installedHooks)
                if (aHook.m_pSystemFunction == installed.m_pSystemFunction && installed.m_ppDetourFunction)
                    aHook.m_pSystemFunction = *installed.m_ppDetourFunction;
            if (MH_CreateHook(aHook.m_pSystemFunction, aHook.m_pHookFunction, aHook.m_ppDetourFunction) == MH_OK)
            {
                if (MH_EnableHook(aHook.m_pSystemFunction) == MH_OK)
                {
                    m_installedHooks.emplace_back(std::move(aHook));
                    return true;
                }
                MH_RemoveHook(aHook.m_pSystemFunction);
            }
        }
        // A failed layer does not own the target; its destructor must not remove
        // an existing hook (including one installed by another plugin).
        aHook.m_ppDetourFunction = nullptr;
        return false;
    }

    void* FunctionHookManager::Add(void* apFunctionDetour, const char* acpLibraryName, const char* acpMethod) noexcept
    {
        const auto pRealFunctionThunk = GetImportedFunction(nullptr, acpLibraryName, acpMethod);

        if (!pRealFunctionThunk)
            return nullptr;

        const auto pRealFunction = *pRealFunctionThunk;
        if (PatchesSuppressed())
            return pRealFunction;

        const vp::ScopedContext thunkMemory(pRealFunctionThunk, sizeof(void*));
        thunkMemory.Write(apFunctionDetour);

        m_iatHooks.emplace_back(IATHook{ pRealFunctionThunk, pRealFunction });

        return pRealFunction;
    }

    static void** GetImportedFunction(const wchar_t *acpModuleName, const char* acpLibraryName, const char* acpMethod) noexcept
    {
        const auto pBase = GetModuleHandleW(acpModuleName);

        const auto pImageDosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(pBase);
        auto pImageNtHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(RtlOffsetToPointer(pBase, pImageDosHeader->e_lfanew));

        const auto pVirtualAddress = pImageNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;

        for (auto pImageImportDescriptor = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(RtlOffsetToPointer(pBase, pVirtualAddress)); pImageImportDescriptor->Name; ++pImageImportDescriptor)
        {
            const auto pLibraryName = reinterpret_cast<const char*>RtlOffsetToPointer(pBase, pImageImportDescriptor->Name);

            if (!_stricmp(pLibraryName, acpLibraryName))
            {
                auto pImportAddressTable = reinterpret_cast<uintptr_t*>(RtlOffsetToPointer(pBase, pImageImportDescriptor->FirstThunk));
                auto pImageThunkData = reinterpret_cast<IMAGE_THUNK_DATA*>(RtlOffsetToPointer(pBase, pImageImportDescriptor->OriginalFirstThunk));

                while (const auto pOrdinal = pImageThunkData->u1.Ordinal)
                {
                    const auto pFunctionName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(RtlOffsetToPointer(pBase, pImageThunkData->u1.AddressOfData))->Name;

                    if (IMAGE_SNAP_BY_ORDINAL(pOrdinal))
                    {
                        // Skip ordinal functions, we just want named functions
                    }
                    else if (!_stricmp(pFunctionName, acpMethod))
                    {
                        return reinterpret_cast<void**>(pImportAddressTable);
                    }

                    ++pImageThunkData;
                    ++pImportAddressTable;
                }

                return nullptr;
            }
        }

        return nullptr;
    }
}
