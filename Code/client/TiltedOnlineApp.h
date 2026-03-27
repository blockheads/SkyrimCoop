#pragma once

#ifdef HAS_DIRECTXTK
#include <d3d11.h>
#endif

#include "CrashHandler.h"

struct TiltedOnlineApp final : App
{
    TiltedOnlineApp();
    virtual ~TiltedOnlineApp();

    TiltedOnlineApp(TiltedOnlineApp&&) = delete;
    TiltedOnlineApp(const TiltedOnlineApp&) = delete;
    TiltedOnlineApp& operator=(TiltedOnlineApp&&) = delete;
    TiltedOnlineApp& operator=(const TiltedOnlineApp&) = delete;

    [[nodiscard]] void* GetMainAddress() const override;

    bool BeginMain() override;
    bool EndMain() override;

    void Update() override;

    bool Attach() override;
    bool Detach() override;

    static void InstallHooks2();

protected:
    void InstallHooks();
    void UninstallHooks();

private:
#ifdef HAS_DIRECTXTK
    void ApplyNvidiaFix() noexcept;
#endif
    CrashHandler m_crashHandler;
#ifdef HAS_DIRECTXTK
    ID3D11Device* m_pDevice = nullptr;
#endif
};
