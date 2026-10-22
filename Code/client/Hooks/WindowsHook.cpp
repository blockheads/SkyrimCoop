#include <Hooks/WindowsHook.h>

#include <Hooks/FunctionHook.h>

using TRegisterClassA = ATOM(*)(const WNDCLASSA*);
using TRegisterClassW = ATOM(*)(const WNDCLASSW*);
using TRegisterClassExA = ATOM(*)(const WNDCLASSEXA*);
using TRegisterClassExW = ATOM(*)(const WNDCLASSEXW*);

TRegisterClassA RealRegisterClassA = nullptr;
TRegisterClassW RealRegisterClassW = nullptr;
TRegisterClassExA RealRegisterClassExA = nullptr;
TRegisterClassExW RealRegisterClassExW = nullptr;

static WNDPROC RealWndProc;

namespace Hooks
{

    LRESULT CALLBACK HookWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        auto pCallback = WindowsHook::Get().GetCallback();
        if (pCallback)
        {
            if (pCallback(hwnd, uMsg, wParam, lParam) != 0)
                return 0;
        }

        return RealWndProc(hwnd, uMsg, wParam, lParam);
    }

    ATOM __stdcall HookRegisterClassA(WNDCLASSA* lpWndClass)
    {
        RealWndProc = lpWndClass->lpfnWndProc;
        lpWndClass->lpfnWndProc = HookWndProc;

        return RealRegisterClassA(lpWndClass);
    }

    ATOM __stdcall HookRegisterClassW(WNDCLASSW* lpWndClass)
    {
        RealWndProc = lpWndClass->lpfnWndProc;
        lpWndClass->lpfnWndProc = HookWndProc;

        return RealRegisterClassW(lpWndClass);
    }

    ATOM __stdcall HookRegisterClassExA(WNDCLASSEXA* lpWndClass)
    {
        RealWndProc = lpWndClass->lpfnWndProc;
        lpWndClass->lpfnWndProc = HookWndProc;

        return RealRegisterClassExA(lpWndClass);
    }

    ATOM __stdcall HookRegisterClassExW(WNDCLASSEXW* lpWndClass)
    {
        RealWndProc = lpWndClass->lpfnWndProc;
        lpWndClass->lpfnWndProc = HookWndProc;

        return RealRegisterClassExW(lpWndClass);
    }

    // The process's top-level windows, visited with this process id.
    static void ForEachOwnWindow(BOOL(CALLBACK* apVisit)(HWND, LPARAM))
    {
        EnumWindows(apVisit, static_cast<LPARAM>(GetCurrentProcessId()));
    }

    // Plugin unload (hot reload): windows must not keep calling into the unloaded DLL.
    static void RestoreWndProc()
    {
        ForEachOwnWindow([](HWND aWindow, LPARAM aProcess) -> BOOL {
            DWORD process = 0;
            GetWindowThreadProcessId(aWindow, &process);
            if (process != static_cast<DWORD>(aProcess) || !RealWndProc)
                return TRUE;
            if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(aWindow, GWLP_WNDPROC)) == HookWndProc)
                SetWindowLongPtrW(aWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RealWndProc));
            if (reinterpret_cast<WNDPROC>(GetClassLongPtrW(aWindow, GCLP_WNDPROC)) == HookWndProc)
                SetClassLongPtrW(aWindow, GCLP_WNDPROC, reinterpret_cast<LONG_PTR>(RealWndProc));
            return TRUE;
        });
    }

    void WindowsHook::Install() noexcept
    {
        HOOK_IAT(RegisterClassA, "user32.dll");
        HOOK_IAT(RegisterClassW, "user32.dll");
        HOOK_IAT(RegisterClassExA, "user32.dll");
        HOOK_IAT(RegisterClassExW, "user32.dll");

        // Loaded after the game window exists (hot reload): take over its procedure directly.
        ForEachOwnWindow([](HWND aWindow, LPARAM aProcess) -> BOOL {
            DWORD process = 0;
            GetWindowThreadProcessId(aWindow, &process);
            if (process == static_cast<DWORD>(aProcess) && IsWindowVisible(aWindow) &&
                reinterpret_cast<WNDPROC>(GetWindowLongPtrW(aWindow, GWLP_WNDPROC)) != HookWndProc)
                RealWndProc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(aWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookWndProc)));
            return TRUE;
        });
        AtUnload(&RestoreWndProc);
    }

    WindowsHook& WindowsHook::Get() noexcept
    {
        static WindowsHook s_instance;
        return s_instance;
    }

}
