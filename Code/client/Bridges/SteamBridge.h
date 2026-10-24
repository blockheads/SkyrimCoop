#pragma once

#include <Bridges/ISteamBridge.h>

#include <memory>

/// ISteamBridge on the Steamworks flat API of the steam_api64.dll Skyrim loaded (SDK 1.5x interface versions).
class SteamBridge final : public ISteamBridge
{
public:
    SteamBridge() noexcept;
    ~SteamBridge() override;

    bool IsReady() noexcept override;
    SteamId GetLaunchLobby() noexcept override;
    SteamId GetUserId() noexcept override;
    std::string GetUserName() noexcept override;
    Events Poll() noexcept override;

    bool CreateLobby() noexcept override;
    bool JoinLobby(SteamId aLobby) noexcept override;
    LobbyResult PollLobbyRequest() noexcept override;
    SteamId GetLobbyOwner(SteamId aLobby) noexcept override;
    bool SetLobbyData(SteamId aLobby, const std::string& acKey, const std::string& acValue) noexcept override;
    std::string GetLobbyData(SteamId aLobby, const std::string& acKey) noexcept override;
    bool InviteToLobby(SteamId aLobby, SteamId aFriend) noexcept override;
    void LeaveLobby(SteamId aLobby) noexcept override;

    bool Listen() noexcept override;
    void StopListening() noexcept override;
    Connection Connect(SteamId aPeer) noexcept override;
    bool Accept(Connection aConnection) noexcept override;
    bool Send(Connection aConnection, const void* apData, size_t aSize, bool aReliable) noexcept override;
    std::vector<std::vector<uint8_t>> Receive(Connection aConnection) noexcept override;
    void Close(Connection aConnection) noexcept override;
    ConnectionInfo GetConnectionInfo(Connection aConnection) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
};
