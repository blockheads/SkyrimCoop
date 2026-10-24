#include <Bridges/SteamBridge.h>

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <windows.h>

#include <mutex>

namespace
{
constexpr uint32_t kSkyrimAppId = 489830;
constexpr int kLobbyTypeFriendsOnly = 1; // ELobbyType
constexpr int kMaxMembers = 4;
constexpr int kResultOk = 1; // EResult k_EResultOK; LobbyEnter_t's k_EChatRoomEnterResponseSuccess is 1 too

// Callback payloads (isteammatchmaking.h, isteamfriends.h; Windows packs them to 8).
#pragma pack(push, 8)
struct LobbyCreated
{
    int Result;
    uint64_t Lobby;
};
struct LobbyEnter
{
    uint64_t Lobby;
    uint32_t Permissions;
    bool Locked;
    uint32_t Response;
};
struct GameLobbyJoinRequested
{
    uint64_t Lobby;
    uint64_t Friend;
};
#pragma pack(pop)
static_assert(sizeof(LobbyCreated) == 16 && sizeof(LobbyEnter) == 24 && sizeof(GameLobbyJoinRequested) == 16);
constexpr int kLobbyCreatedId = 513, kLobbyEnterId = 504, kGameLobbyJoinRequestedId = 333;

// Steamworks' CCallbackBase (steam_api_internal.h): what SteamAPI_RegisterCallback dispatches to. Same declaration, so
// clang-cl lays out its vtable as the SDK's MSVC build does.
class CallbackBase
{
public:
    virtual void Run(void* apParam) = 0;
    virtual void Run(void* apParam, bool aIoFailure, uint64_t aCall) = 0;
    virtual int GetCallbackSizeBytes() = 0;

protected:
    uint8_t m_flags{};
    int m_callback{};
};
} // namespace

struct SteamBridge::Impl
{
    // The flat API (steam_api_flat.h): each interface's functions take the interface as their first argument.
    HMODULE Module{};
    int (*GetHSteamUser)(){};
    void (*RegisterCallback)(CallbackBase*, int){};
    void (*UnregisterCallback)(CallbackBase*){};
    void* (*User)(){};
    uint64_t (*GetSteamId)(void*){};
    void* (*Friends)(){};
    const char* (*GetPersonaName)(void*){};
    void* (*Utils)(){};
    uint32_t (*GetAppId)(void*){};
    bool (*IsCallCompleted)(void*, uint64_t, bool*){};
    bool (*GetCallResult)(void*, uint64_t, void*, int, int, bool*){};
    void* (*Matchmaking)(){};
    uint64_t (*CreateLobby)(void*, int, int){};
    uint64_t (*JoinLobby)(void*, uint64_t){};
    void (*LeaveLobby)(void*, uint64_t){};
    bool (*InviteUserToLobby)(void*, uint64_t, uint64_t){};
    uint64_t (*GetLobbyOwner)(void*, uint64_t){};
    bool (*SetLobbyData)(void*, uint64_t, const char*, const char*){};
    const char* (*GetLobbyData)(void*, uint64_t, const char*){};
    void* (*Sockets)(){};
    HSteamListenSocket (*CreateListenSocketP2P)(void*, int, int, const SteamNetworkingConfigValue_t*){};
    HSteamNetConnection (*ConnectP2P)(void*, const SteamNetworkingIdentity*, int, int, const SteamNetworkingConfigValue_t*){};
    EResult (*AcceptConnection)(void*, HSteamNetConnection){};
    bool (*CloseConnection)(void*, HSteamNetConnection, int, const char*, bool){};
    bool (*CloseListenSocket)(void*, HSteamListenSocket){};
    EResult (*SendMessage)(void*, HSteamNetConnection, const void*, uint32, int, int64*){};
    int (*ReceiveMessages)(void*, HSteamNetConnection, SteamNetworkingMessage_t**, int){};
    bool (*GetConnectionInfo)(void*, HSteamNetConnection, SteamNetConnectionInfo_t*){};
    void (*RunCallbacks)(void*){};
    void (*ReleaseMessage)(SteamNetworkingMessage_t*){};
    void* (*NetworkingUtils)(){};
    void (*InitRelayNetworkAccess)(void*){};

    bool Loaded{};
    uint64_t Call{};
    bool Creating{};
    HSteamListenSocket ListenSocket{k_HSteamListenSocket_Invalid};

    std::mutex Lock; // the events arrive on the thread running SteamAPI_RunCallbacks (the game's)
    Events Pending;

    struct JoinRequested final : CallbackBase
    {
        explicit JoinRequested(Impl& aImpl) noexcept
            : m_impl(aImpl)
        {
            m_callback = kGameLobbyJoinRequestedId;
        }
        void Run(void* apParam) override
        {
            const auto* pRequest = static_cast<const GameLobbyJoinRequested*>(apParam);
            std::scoped_lock lock(m_impl.Lock);
            m_impl.Pending.JoinRequests.push_back(pRequest->Lobby);
        }
        void Run(void* apParam, bool, uint64_t) override { Run(apParam); }
        int GetCallbackSizeBytes() override { return sizeof(GameLobbyJoinRequested); }

        Impl& m_impl;
    };
    JoinRequested JoinRequestedCallback{*this};
    bool Registered{};

    template <class T> bool Resolve(T& aFunction, const char* acName) noexcept
    {
        aFunction = reinterpret_cast<T>(GetProcAddress(Module, acName));
        return aFunction != nullptr;
    }

    bool Load() noexcept
    {
        if (Loaded)
            return true;
        Module = GetModuleHandleW(L"steam_api64.dll");
        if (!Module)
            return false;
        Loaded = Resolve(GetHSteamUser, "SteamAPI_GetHSteamUser") && Resolve(RegisterCallback, "SteamAPI_RegisterCallback") &&
                 Resolve(UnregisterCallback, "SteamAPI_UnregisterCallback") && Resolve(User, "SteamAPI_SteamUser_v021") &&
                 Resolve(GetSteamId, "SteamAPI_ISteamUser_GetSteamID") && Resolve(Friends, "SteamAPI_SteamFriends_v017") &&
                 Resolve(GetPersonaName, "SteamAPI_ISteamFriends_GetPersonaName") && Resolve(Utils, "SteamAPI_SteamUtils_v010") &&
                 Resolve(GetAppId, "SteamAPI_ISteamUtils_GetAppID") &&
                 Resolve(IsCallCompleted, "SteamAPI_ISteamUtils_IsAPICallCompleted") &&
                 Resolve(GetCallResult, "SteamAPI_ISteamUtils_GetAPICallResult") &&
                 Resolve(Matchmaking, "SteamAPI_SteamMatchmaking_v009") &&
                 Resolve(CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby") &&
                 Resolve(JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby") &&
                 Resolve(LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby") &&
                 Resolve(InviteUserToLobby, "SteamAPI_ISteamMatchmaking_InviteUserToLobby") &&
                 Resolve(GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner") &&
                 Resolve(SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData") &&
                 Resolve(GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData") &&
                 Resolve(Sockets, "SteamAPI_SteamNetworkingSockets_SteamAPI_v012") &&
                 Resolve(CreateListenSocketP2P, "SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P") &&
                 Resolve(ConnectP2P, "SteamAPI_ISteamNetworkingSockets_ConnectP2P") &&
                 Resolve(AcceptConnection, "SteamAPI_ISteamNetworkingSockets_AcceptConnection") &&
                 Resolve(CloseConnection, "SteamAPI_ISteamNetworkingSockets_CloseConnection") &&
                 Resolve(CloseListenSocket, "SteamAPI_ISteamNetworkingSockets_CloseListenSocket") &&
                 Resolve(SendMessage, "SteamAPI_ISteamNetworkingSockets_SendMessageToConnection") &&
                 Resolve(ReceiveMessages, "SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection") &&
                 Resolve(GetConnectionInfo, "SteamAPI_ISteamNetworkingSockets_GetConnectionInfo") &&
                 Resolve(RunCallbacks, "SteamAPI_ISteamNetworkingSockets_RunCallbacks") &&
                 Resolve(ReleaseMessage, "SteamAPI_SteamNetworkingMessage_t_Release") &&
                 Resolve(NetworkingUtils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004") &&
                 Resolve(InitRelayNetworkAccess, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
        if (!Loaded)
            spdlog::error("SteamBridge: steam_api64.dll lacks a function this plugin calls (an older Steam API?)");
        return Loaded;
    }

    bool Ready() noexcept
    {
        if (!Load() || !GetHSteamUser() || !Utils() || GetAppId(Utils()) != kSkyrimAppId)
            return false;
        if (!Registered)
        {
            RegisterCallback(&JoinRequestedCallback, kGameLobbyJoinRequestedId);
            // Starts finding relays now, so the first connection need not wait for it.
            InitRelayNetworkAccess(NetworkingUtils());
            Registered = true;
        }
        return true;
    }

    // Steam calls it from RunCallbacks, in Poll (k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged).
    static void OnConnectionStatus(SteamNetConnectionStatusChangedCallback_t* apStatus);
    static Impl* s_pInstance;

    void ConnectionStatus(const SteamNetConnectionStatusChangedCallback_t& acStatus) noexcept
    {
        std::scoped_lock lock(Lock);
        switch (acStatus.m_info.m_eState)
        {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (acStatus.m_info.m_hListenSocket != k_HSteamListenSocket_Invalid)
                Pending.Incoming.push_back(acStatus.m_hConn);
            break;
        case k_ESteamNetworkingConnectionState_Connected: Pending.Connected.push_back(acStatus.m_hConn); break;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            Pending.Closed.emplace_back(acStatus.m_hConn, acStatus.m_info.m_szEndDebug);
            CloseConnection(Sockets(), acStatus.m_hConn, 0, nullptr, false); // frees Steam's handle
            break;
        default: break;
        }
    }

    SteamNetworkingConfigValue_t StatusCallbackOption() noexcept
    {
        SteamNetworkingConfigValue_t option{};
        option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                      reinterpret_cast<void*>(&Impl::OnConnectionStatus));
        return option;
    }
};

SteamBridge::Impl* SteamBridge::Impl::s_pInstance = nullptr;

void SteamBridge::Impl::OnConnectionStatus(SteamNetConnectionStatusChangedCallback_t* apStatus)
{
    if (s_pInstance && apStatus)
        s_pInstance->ConnectionStatus(*apStatus);
}

SteamBridge::SteamBridge() noexcept
    : m_pImpl(std::make_unique<Impl>())
{
    Impl::s_pInstance = m_pImpl.get();
}

SteamBridge::~SteamBridge()
{
    // A hot reload unloads this code: Steam must not call into it afterwards.
    if (m_pImpl->Registered)
        m_pImpl->UnregisterCallback(&m_pImpl->JoinRequestedCallback);
    StopListening();
    Impl::s_pInstance = nullptr;
}

ISteamBridge::SteamId SteamBridge::GetLaunchLobby() noexcept
{
    const wchar_t* cpLine = GetCommandLineW();
    if (!cpLine)
        return 0;
    const std::wstring_view cWide(cpLine);
    std::string line(cWide.size(), ' ');
    for (size_t i = 0; i < cWide.size(); ++i)
        line[i] = cWide[i] < 0x80 ? static_cast<char>(cWide[i]) : '?';
    return ParseConnectLobby(line);
}

bool SteamBridge::IsReady() noexcept
{
    return m_pImpl->Ready();
}

ISteamBridge::SteamId SteamBridge::GetUserId() noexcept
{
    return IsReady() ? m_pImpl->GetSteamId(m_pImpl->User()) : 0;
}

std::string SteamBridge::GetUserName() noexcept
{
    const char* pName = IsReady() ? m_pImpl->GetPersonaName(m_pImpl->Friends()) : nullptr;
    return pName ? pName : "";
}

ISteamBridge::Events SteamBridge::Poll() noexcept
{
    if (!IsReady())
        return {};
    m_pImpl->RunCallbacks(m_pImpl->Sockets());
    std::scoped_lock lock(m_pImpl->Lock);
    return std::exchange(m_pImpl->Pending, {});
}

bool SteamBridge::CreateLobby() noexcept
{
    if (!IsReady() || m_pImpl->Call)
        return false;
    m_pImpl->Call = m_pImpl->CreateLobby(m_pImpl->Matchmaking(), kLobbyTypeFriendsOnly, kMaxMembers);
    m_pImpl->Creating = true;
    return m_pImpl->Call != 0;
}

bool SteamBridge::JoinLobby(SteamId aLobby) noexcept
{
    if (!IsReady() || m_pImpl->Call)
        return false;
    m_pImpl->Call = m_pImpl->JoinLobby(m_pImpl->Matchmaking(), aLobby);
    m_pImpl->Creating = false;
    return m_pImpl->Call != 0;
}

ISteamBridge::LobbyResult SteamBridge::PollLobbyRequest() noexcept
{
    auto& impl = *m_pImpl;
    bool failed = false;
    if (!impl.Call || !impl.IsCallCompleted(impl.Utils(), impl.Call, &failed))
        return {};
    const uint64_t cCall = std::exchange(impl.Call, 0);
    if (failed)
        return {.Done = true, .Reason = -1};
    if (impl.Creating)
    {
        LobbyCreated created{};
        if (!impl.GetCallResult(impl.Utils(), cCall, &created, sizeof(created), kLobbyCreatedId, &failed) || failed)
            return {.Done = true, .Reason = -1};
        return {.Done = true, .Lobby = created.Result == kResultOk ? created.Lobby : 0, .Reason = created.Result};
    }
    LobbyEnter entered{};
    if (!impl.GetCallResult(impl.Utils(), cCall, &entered, sizeof(entered), kLobbyEnterId, &failed) || failed)
        return {.Done = true, .Reason = -1};
    const int cReason = static_cast<int>(entered.Response);
    return {.Done = true, .Lobby = cReason == kResultOk ? entered.Lobby : 0, .Reason = cReason};
}

ISteamBridge::SteamId SteamBridge::GetLobbyOwner(SteamId aLobby) noexcept
{
    return IsReady() ? m_pImpl->GetLobbyOwner(m_pImpl->Matchmaking(), aLobby) : 0;
}

bool SteamBridge::SetLobbyData(SteamId aLobby, const std::string& acKey, const std::string& acValue) noexcept
{
    return IsReady() && m_pImpl->SetLobbyData(m_pImpl->Matchmaking(), aLobby, acKey.c_str(), acValue.c_str());
}

std::string SteamBridge::GetLobbyData(SteamId aLobby, const std::string& acKey) noexcept
{
    const char* pValue = IsReady() ? m_pImpl->GetLobbyData(m_pImpl->Matchmaking(), aLobby, acKey.c_str()) : nullptr;
    return pValue ? pValue : "";
}

bool SteamBridge::InviteToLobby(SteamId aLobby, SteamId aFriend) noexcept
{
    return IsReady() && m_pImpl->InviteUserToLobby(m_pImpl->Matchmaking(), aLobby, aFriend);
}

void SteamBridge::LeaveLobby(SteamId aLobby) noexcept
{
    if (IsReady() && aLobby)
        m_pImpl->LeaveLobby(m_pImpl->Matchmaking(), aLobby);
}

bool SteamBridge::Listen() noexcept
{
    auto& impl = *m_pImpl;
    if (!IsReady())
        return false;
    if (impl.ListenSocket != k_HSteamListenSocket_Invalid)
        return true;
    const SteamNetworkingConfigValue_t cOption = impl.StatusCallbackOption();
    impl.ListenSocket = impl.CreateListenSocketP2P(impl.Sockets(), 0, 1, &cOption);
    return impl.ListenSocket != k_HSteamListenSocket_Invalid;
}

void SteamBridge::StopListening() noexcept
{
    auto& impl = *m_pImpl;
    if (impl.ListenSocket != k_HSteamListenSocket_Invalid && impl.Loaded)
        impl.CloseListenSocket(impl.Sockets(), impl.ListenSocket);
    impl.ListenSocket = k_HSteamListenSocket_Invalid;
}

ISteamBridge::Connection SteamBridge::Connect(SteamId aPeer) noexcept
{
    auto& impl = *m_pImpl;
    if (!IsReady())
        return 0;
    SteamNetworkingIdentity peer{};
    peer.SetSteamID64(aPeer);
    const SteamNetworkingConfigValue_t cOption = impl.StatusCallbackOption();
    const HSteamNetConnection cConnection = impl.ConnectP2P(impl.Sockets(), &peer, 0, 1, &cOption);
    return cConnection == k_HSteamNetConnection_Invalid ? 0 : cConnection;
}

bool SteamBridge::Accept(Connection aConnection) noexcept
{
    return IsReady() && m_pImpl->AcceptConnection(m_pImpl->Sockets(), aConnection) == k_EResultOK;
}

bool SteamBridge::Send(Connection aConnection, const void* apData, size_t aSize, bool aReliable) noexcept
{
    if (!IsReady())
        return false;
    const int cFlags = aReliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoNagle;
    return m_pImpl->SendMessage(m_pImpl->Sockets(), aConnection, apData, static_cast<uint32>(aSize), cFlags, nullptr) ==
           k_EResultOK;
}

std::vector<std::vector<uint8_t>> SteamBridge::Receive(Connection aConnection) noexcept
{
    std::vector<std::vector<uint8_t>> messages;
    if (!IsReady())
        return messages;
    SteamNetworkingMessage_t* pMessages[64]{};
    int count = 0;
    while ((count = m_pImpl->ReceiveMessages(m_pImpl->Sockets(), aConnection, pMessages, 64)) > 0)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto* pData = static_cast<const uint8_t*>(pMessages[i]->m_pData);
            messages.emplace_back(pData, pData + pMessages[i]->m_cbSize);
            m_pImpl->ReleaseMessage(pMessages[i]);
        }
    }
    return messages;
}

void SteamBridge::Close(Connection aConnection) noexcept
{
    if (IsReady() && aConnection)
        m_pImpl->CloseConnection(m_pImpl->Sockets(), aConnection, 0, "SkyrimCoop: closed", true);
}

ISteamBridge::ConnectionInfo SteamBridge::GetConnectionInfo(Connection aConnection) noexcept
{
    SteamNetConnectionInfo_t info{};
    if (!IsReady() || !m_pImpl->GetConnectionInfo(m_pImpl->Sockets(), aConnection, &info))
        return {};
    return {.Peer = info.m_identityRemote.GetSteamID64(),
            .Relayed = (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) != 0,
            .Description = info.m_szConnectionDescription};
}
