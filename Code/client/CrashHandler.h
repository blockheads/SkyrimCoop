#pragma once

class CrashHandler
{
    PVOID m_handler;
    static LPTOP_LEVEL_EXCEPTION_FILTER m_pUnhandled; // For remembering "original" UnhandledExceptionFilter

  public:
    CrashHandler();
    ~CrashHandler();

    static void RemovePreviousDump(std::filesystem::path path);
    // Call once per frame from the main loop; the first call starts the hang watchdog.
    static void OnMainLoopTick() noexcept;
    // Extra state to log when the main loop stalls (runs on the watchdog thread: read-only peeks).
    static void SetHangDiagnostic(void (*apDiagnostic)()) noexcept;
    // A function that waits for a spin lock {owner thread id, count}, and where the lock is in a frame of it: the hang
    // dump then names each stuck thread's lock and its owner.
    using TSpinLockOfFrame = DWORD64 (*)(const CONTEXT& acFrame);
    static void SetSpinLockWait(DWORD64 aFunction, TSpinLockOfFrame apLockOfFrame) noexcept;
    static inline LPTOP_LEVEL_EXCEPTION_FILTER GetOriginalUnhandledExceptionFilter()
    {
        return m_pUnhandled;
    }
};
