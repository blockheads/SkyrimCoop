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
    static inline LPTOP_LEVEL_EXCEPTION_FILTER GetOriginalUnhandledExceptionFilter()
    {
        return m_pUnhandled;
    }
};
