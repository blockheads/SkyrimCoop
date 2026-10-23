#pragma once

// Tier 2's network conditions (docs/design/testing.md, "Network conditions"): a UDP relay on loopback between a
// guest's enet client and the host's server, holding each datagram for a delay plus jitter (so some arrive out of
// order) and dropping some, as the internet between two players would. Pumped by Party on the test's thread.
#include <netinet/in.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace GameTest
{
struct NetConditions
{
    int DelayMs = 0;  // each way
    int JitterMs = 0; // up to this much more, per datagram
    int LossPercent = 0;
    uint32_t Seed = 1;

    [[nodiscard]] bool Any() const noexcept
    {
        return DelayMs || JitterMs || LossPercent;
    }
    [[nodiscard]] std::string Describe() const;
    // From COOP_NET ("delay=80,jitter=40,loss=5,seed=3"); none when it is unset.
    [[nodiscard]] static NetConditions FromEnvironment();
};

struct UdpRelay
{
    UdpRelay(uint16_t aHostPort, const NetConditions& acConditions, uint32_t aSeed);
    ~UdpRelay();
    UdpRelay(const UdpRelay&) = delete;
    UdpRelay& operator=(const UdpRelay&) = delete;

    // Where the guest connects.
    [[nodiscard]] uint16_t Port() const noexcept
    {
        return m_port;
    }
    // Takes what arrived either way and sends what is due.
    void Pump();

    size_t Relayed = 0;
    size_t Dropped = 0;

  private:
    using Clock = std::chrono::steady_clock;
    struct Datagram
    {
        bool ToHost = false;
        std::vector<char> Data;
    };

    void Receive(int aSocket, bool aToHost);

    int m_guestSide = -1; // the guest's client sends here
    int m_hostSide = -1;  // sends to the host's server and gets its answers
    sockaddr_in m_host{};
    std::optional<sockaddr_in> m_guest;
    uint16_t m_port = 0;
    NetConditions m_conditions;
    std::mt19937 m_random;
    std::multimap<Clock::time_point, Datagram> m_held;
};
} // namespace GameTest
