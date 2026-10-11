#include <TiltedOnlinePCH.h>
#include "CommandServer.h"

#include <winsock2.h>
#include <fstream>

namespace CoopReload
{
static void CloseSocket(uintptr_t aSocket)
{
    closesocket(static_cast<SOCKET>(aSocket));
}

static void SetNonBlocking(SOCKET aSocket)
{
    u_long nonBlocking = 1;
    ioctlsocket(aSocket, FIONBIO, &nonBlocking);
}

CommandServer::~CommandServer()
{
    for (const Client& cClient : m_clients)
        CloseSocket(cClient.Socket);
    if (m_listener != ~uintptr_t(0))
        CloseSocket(m_listener);
}

bool CommandServer::Start(const std::string& aDirectory, THandler aHandler)
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return false;

    const SOCKET cListener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (cListener == INVALID_SOCKET)
        return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0; // Any free port: two games run side by side.
    int length = sizeof(address);
    if (bind(cListener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(cListener, 4) != 0 ||
        getsockname(cListener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
    {
        closesocket(cListener);
        return false;
    }
    SetNonBlocking(cListener);

    m_listener = cListener;
    m_handler = std::move(aHandler);

    const int cPort = ntohs(address.sin_port);
    std::ofstream(aDirectory + "/rpc.json", std::ios::trunc)
        << "{\"port\": " << cPort << ", \"pid\": " << GetCurrentProcessId() << "}\n";
    spdlog::info("Command server listening on 127.0.0.1:{}", cPort);
    return true;
}

void CommandServer::Poll()
{
    if (m_listener == ~uintptr_t(0))
        return;

    for (SOCKET accepted; (accepted = accept(static_cast<SOCKET>(m_listener), nullptr, nullptr)) != INVALID_SOCKET;)
    {
        SetNonBlocking(accepted);
        m_clients.push_back({static_cast<uintptr_t>(accepted), {}, {}});
    }

    for (Client& client : m_clients)
    {
        const auto cSocket = static_cast<SOCKET>(client.Socket);
        char buffer[4096];
        int received;
        while ((received = recv(cSocket, buffer, sizeof(buffer), 0)) > 0)
            client.Inbound.append(buffer, received);
        if (received == 0 || (received < 0 && WSAGetLastError() != WSAEWOULDBLOCK))
        {
            CloseSocket(client.Socket);
            client.Socket = ~uintptr_t(0);
            continue;
        }

        for (size_t end; (end = client.Inbound.find('\n')) != std::string::npos;)
        {
            const std::string cRequest = client.Inbound.substr(0, end);
            client.Inbound.erase(0, end + 1);
            client.Outbound += m_handler(cRequest);
            client.Outbound += '\n';
        }

        while (!client.Outbound.empty())
        {
            const int cSent = send(cSocket, client.Outbound.data(), static_cast<int>(client.Outbound.size()), 0);
            if (cSent <= 0)
                break; // Full send buffer: the rest goes next frame.
            client.Outbound.erase(0, cSent);
        }
    }

    std::erase_if(m_clients, [](const Client& acClient) { return acClient.Socket == ~uintptr_t(0); });
}
} // namespace CoopReload
