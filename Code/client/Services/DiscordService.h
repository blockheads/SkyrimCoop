#pragma once

#ifdef HAS_DISCORD

#include <discord.h>

class DiscordService final : public entt::registry
{
public:
    DiscordService(entt::dispatcher&);
    ~DiscordService();

    bool Init();

    // noop currently
    void Update();

    inline auto& GetUser() const noexcept { return m_userData; }

    // update the presence state
    // then request an update
    inline auto& GetPresence() { return m_ActivityState; }
    void UpdatePresence(bool newTimeStamp);

    void WndProcHandler(HWND, UINT, WPARAM, LPARAM);

private:
    void InitOverlay(struct IDXGISwapChain*);
    void OnLocationChangeEvent() noexcept;

    entt::scoped_connection m_cellChangeConnection;

    bool m_bCustomPresence = false;
    bool m_bOverlayEnabled = false;
    bool m_bRequestThreadKillHack = false;

    IDiscordCore* m_pCore = nullptr;
    IDiscordUserManager* m_pUserMgr = nullptr;
    IDiscordActivityManager* m_pActivity = nullptr;
    IDiscordApplicationManager* m_pAppMgr = nullptr;
    IDiscordOverlayManager* m_pOverlayMgr = nullptr;

    DiscordUser m_userData{};
    DiscordActivity m_ActivityState{};

    uint32_t m_lastLocationId = 0;
    uint32_t m_lastWorldspaceId = 0;

    static void OnUserUpdate(void* userp);

    static IDiscordUserEvents s_mUserEvents;
};

#else
// No-op stub (per D-02)
struct DiscordService
{
    struct StubUser { int64_t id = 0; };

    DiscordService(entt::dispatcher&) {}
    ~DiscordService() = default;
    bool Init() { return false; }
    void Update() {}
    const StubUser& GetUser() const noexcept { return m_stubUser; }
    void WndProcHandler(HWND, UINT, WPARAM, LPARAM) {}

private:
    StubUser m_stubUser{};
};
#endif
