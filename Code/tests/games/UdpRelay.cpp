#include "UdpRelay.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <format>
#include <sstream>

namespace GameTest
{
namespace
{
// A non-blocking UDP socket on 127.0.0.1, on a port the system picks.
int BindLoopback(uint16_t& aPort)
{
    const int cSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(cSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    ::fcntl(cSocket, F_SETFL, ::fcntl(cSocket, F_GETFL) | O_NONBLOCK);
    socklen_t length = sizeof(address);
    ::getsockname(cSocket, reinterpret_cast<sockaddr*>(&address), &length);
    aPort = ntohs(address.sin_port);
    return cSocket;
}
} // namespace

std::string NetConditions::Describe() const
{
    return std::format("delay {} ms, jitter {} ms, loss {}%, seed {}", DelayMs, JitterMs, LossPercent, Seed);
}

NetConditions NetConditions::FromEnvironment()
{
    NetConditions conditions;
    const char* cpValue = std::getenv("COOP_NET");
    if (!cpValue)
        return conditions;
    std::stringstream stream(cpValue);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        const auto cEquals = item.find('=');
        if (cEquals == std::string::npos)
            continue;
        const std::string cKey = item.substr(0, cEquals);
        const int cValue = std::atoi(item.c_str() + cEquals + 1);
        if (cKey == "delay")
            conditions.DelayMs = cValue;
        else if (cKey == "jitter")
            conditions.JitterMs = cValue;
        else if (cKey == "loss")
            conditions.LossPercent = cValue;
        else if (cKey == "seed")
            conditions.Seed = static_cast<uint32_t>(cValue);
    }
    return conditions;
}

UdpRelay::UdpRelay(uint16_t aHostPort, const NetConditions& acConditions, uint32_t aSeed)
    : m_conditions(acConditions), m_random(aSeed)
{
    m_guestSide = BindLoopback(m_port);
    uint16_t hostSidePort = 0;
    m_hostSide = BindLoopback(hostSidePort);
    m_host.sin_family = AF_INET;
    m_host.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    m_host.sin_port = htons(aHostPort);
}

UdpRelay::~UdpRelay()
{
    ::close(m_guestSide);
    ::close(m_hostSide);
}

void UdpRelay::Receive(int aSocket, bool aToHost)
{
    char buffer[65536];
    while (true)
    {
        sockaddr_in from{};
        socklen_t length = sizeof(from);
        const auto cSize = ::recvfrom(aSocket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &length);
        if (cSize < 0)
            return;
        // The guest's client may come back on another port (a new connection): its answers go there.
        if (aToHost)
            m_guest = from;
        if (std::uniform_int_distribution<int>(0, 99)(m_random) < m_conditions.LossPercent)
        {
            ++Dropped;
            continue;
        }
        const int cJitter =
            m_conditions.JitterMs ? std::uniform_int_distribution<int>(0, m_conditions.JitterMs)(m_random) : 0;
        const auto cDue = Clock::now() + std::chrono::milliseconds(m_conditions.DelayMs + cJitter);
        m_held.emplace(cDue, Datagram{aToHost, std::vector<char>(buffer, buffer + cSize)});
    }
}

void UdpRelay::Pump()
{
    Receive(m_guestSide, true);
    Receive(m_hostSide, false);
    const auto cNow = Clock::now();
    while (!m_held.empty() && m_held.begin()->first <= cNow)
    {
        const auto& cDatagram = m_held.begin()->second;
        if (cDatagram.ToHost)
            ::sendto(m_hostSide, cDatagram.Data.data(), cDatagram.Data.size(), 0,
                     reinterpret_cast<const sockaddr*>(&m_host), sizeof(m_host));
        else if (m_guest)
            ::sendto(m_guestSide, cDatagram.Data.data(), cDatagram.Data.size(), 0,
                     reinterpret_cast<const sockaddr*>(&*m_guest), sizeof(*m_guest));
        ++Relayed;
        m_held.erase(m_held.begin());
    }
}
} // namespace GameTest
