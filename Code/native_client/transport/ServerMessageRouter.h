#pragma once

#include <cstdint>
#include <functional>
#include <TiltedCore/Meta.hpp>

struct ServerMessageRouter
{
    TP_NOCOPYMOVE(ServerMessageRouter);

    ServerMessageRouter() = default;

    // Process raw server message bytes (from loopback callback)
    void OnServerMessage(const void* apData, uint32_t aSize);

    // Opcode-specific callbacks (set by native services)
    using AuthCallback = std::function<void(bool aAccepted, uint32_t aPlayerId)>;
    using MoveCallback = std::function<void(uint32_t aServerId, float aX, float aY, float aZ, float aRx, float aRy)>;

    void SetAuthCallback(AuthCallback aCb);
    void SetMoveCallback(MoveCallback aCb);

    [[nodiscard]] uint32_t GetLastOpcode() const { return m_lastOpcode; }
    [[nodiscard]] bool IsAuthenticated() const { return m_authenticated; }

private:
    AuthCallback m_onAuth;
    MoveCallback m_onMove;
    uint32_t m_lastOpcode{0};
    bool m_authenticated{false};
};
