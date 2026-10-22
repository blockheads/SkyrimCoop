#pragma once

#include <d3d11.h>

#include "CrashHandler.h"

// The plugin's lifetime: logging and the crash handler from SKSE's load, the world once the game runs.
struct TiltedOnlineApp final
{
    TiltedOnlineApp();
    ~TiltedOnlineApp();

    TiltedOnlineApp(TiltedOnlineApp&&) = delete;
    TiltedOnlineApp(const TiltedOnlineApp&) = delete;
    TiltedOnlineApp& operator=(TiltedOnlineApp&&) = delete;
    TiltedOnlineApp& operator=(const TiltedOnlineApp&) = delete;

    // Every hook the plugin's files registered (Initializer), installed at SKSE's load.
    static void InstallHooks();
    // Creates the world (SKSE's load, after InstallHooks).
    void Start();
    // Every frame (the Papyrus VM's update hook).
    void Update();

private:
    void ApplyNvidiaFix() noexcept;
    CrashHandler m_crashHandler;
    ID3D11Device* m_pDevice = nullptr;
};
