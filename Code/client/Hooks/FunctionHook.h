#pragma once

#include <windows.h>

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <Hooks/Memory.h>
#include <VersionDb.h>

namespace Hooks
{
    // Told of every observed hook call, with the hook's name and its first argument (for a member
    // hook, the object it runs on), before the hook runs. The client records them as crash
    // breadcrumbs. Set it before hooks are added; hooks the filter refuses (very frequent ones)
    // are not observed.
    using HookObserver = void (*)(const char* acpHookName, const void* apFirstArgument);
    using HookObserverFilter = bool (*)(const char* acpHookName);
    void SetHookObserver(HookObserver aObserver, HookObserverFilter aFilter) noexcept;

    struct FunctionHook
    {
        FunctionHook() noexcept;
        explicit FunctionHook(void** appSystemFunction, void* apHookFunction, const char* acpName = nullptr) noexcept;
        ~FunctionHook() noexcept;
        FunctionHook(const FunctionHook& acRhs) = delete;
        FunctionHook(FunctionHook&& aRhs) noexcept;
        FunctionHook& operator=(const FunctionHook& acRhs) = delete;
        FunctionHook& operator=(FunctionHook&& aRhs) noexcept;

    protected:

        friend class FunctionHookManager;

        void** m_ppDetourFunction;
        void* m_pSystemFunction;
        void* m_pHookFunction;
        const char* m_pName;
    };

    class FunctionHookManager
    {
    protected:

        struct IATHook
        {
            void** pThunk;
            void* pOriginal;
        };

    public:

        FunctionHookManager(const FunctionHookManager&) = delete;
        FunctionHookManager(FunctionHookManager&&) = delete;

        FunctionHookManager& operator=(const FunctionHookManager&) = delete;
        FunctionHookManager& operator=(FunctionHookManager&&) = delete;

        void InstallDelayedHooks() noexcept;
        void UninstallHooks() noexcept;
        // Disables and removes every hook and puts the import tables back (hot reload unloads
        // the plugin; UninstallHooks leaves the hooks in place for process exit).
        void RemoveAllHooks() noexcept;

        void Add(FunctionHook aFunctionHook, bool aDelayed = false) noexcept;
        // A probe: the engine function runs as before, but every call is reported to the hook
        // observer first (name, first argument). False when it cannot be hooked (already hooked
        // by MinHook, say) or there is no observer.
        bool AddProbe(void* apFunction, const char* acpName) noexcept;
        void* Add(void* apFunctionDetour, const char* acpLibraryName, const char* acpMethod) noexcept;

        // Template overload for function references (GCC does not implicitly convert to void*)
        template<class T>
        void* Add(T& aFunctionDetour, const char* acpLibraryName, const char* acpMethod) noexcept
        {
            return Add(reinterpret_cast<void*>(&aFunctionDetour), acpLibraryName, acpMethod);
        }

        template<class T, class U>
        void Add(T** aSystemFunction, U* aHookFunction, bool aDelayed = false, const char* acpName = nullptr) noexcept
        {
            Add(FunctionHook(reinterpret_cast<void**>(aSystemFunction), reinterpret_cast<void*>(aHookFunction), acpName), aDelayed);
        }

        static FunctionHookManager& GetInstance() noexcept
        {
            static FunctionHookManager s_instance;
            return s_instance;
        }

    private:

        FunctionHookManager() noexcept;
        ~FunctionHookManager() noexcept;

        std::vector<FunctionHook> m_delayedHooks;
        std::vector<FunctionHook> m_installedHooks;
        std::vector<IATHook> m_iatHooks;
    };

    template<class T, class Func>
    Func HookVTable(T* pInstance, const size_t aIndex, Func aFunctionPtr)
    {
        auto pVTable = *reinterpret_cast<uintptr_t * *>(pInstance);
        Func pRealFunction = reinterpret_cast<Func>(pVTable[aIndex]);
        if (PatchesSuppressed())
            return pRealFunction;

        RecordPatch(&pVTable[aIndex], sizeof(pVTable[aIndex]));
        const vp::ScopedContext memory(&pVTable[aIndex], sizeof(&pVTable[aIndex]));
        memory.Write(aFunctionPtr);

        return pRealFunction;
    }

    // Resolve an Address Library ID and keep the original hook name for crash breadcrumbs.
    template<uint32_t Id, class T, class U>
    void Hook(T** appSystemFunction, U* apHookFunction, const char* acpName, bool aDelayed = true) noexcept
    {
        static VersionDbPtr<T> s_function(Id);
        *appSystemFunction = s_function.Get();
        FunctionHookManager::GetInstance().Add(appSystemFunction, apHookFunction, aDelayed, acpName);
    }
}

#define HOOK(systemFunction, hookFunction) Hooks::FunctionHookManager::GetInstance().Add(systemFunction, hookFunction, true, #hookFunction)
#define HOOK_IMMEDIATE(systemFunction, hookFunction) Hooks::FunctionHookManager::GetInstance().Add(systemFunction, hookFunction, false, #hookFunction)

#define HOOK_ID(id, systemFunction, hookFunction) Hooks::Hook<id>(systemFunction, hookFunction, #hookFunction)
#define HOOK_ID_IMMEDIATE(id, systemFunction, hookFunction) Hooks::Hook<id>(systemFunction, hookFunction, #hookFunction, false)

#define HOOK_IAT(functionName, libraryName) Real ##functionName = (T ##functionName)Hooks::FunctionHookManager::GetInstance().Add(Hook ## functionName, libraryName, #functionName)

#define HOOK_COMMIT Hooks::FunctionHookManager::GetInstance().InstallDelayedHooks();

#define EMPTY_HOOK_PLACEHOLDER \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop(); \
__nop();
