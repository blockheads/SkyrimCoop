// HostNetwork runs on the host's game frame (HostService::OnUpdate): it must never block it.
#include <Services/Host/HostNetwork.h>

#include <network/Client.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <format>
#include <functional>

namespace
{
// A guest's end of the connection, nothing more.
struct Guest final : Net::Client
{
    // Closes while OnDisconnected is still ours (~Client's Close would call it pure).
    ~Guest() override
    {
        Close();
    }

    void OnConsume(const void*, uint32_t) override
    {
    }
    void OnConnected() override
    {
    }
    void OnDisconnected(EDisconnectReason) override
    {
    }
    void OnUpdate() override
    {
    }
};

std::chrono::milliseconds Timed(const std::function<void()>& acAction)
{
    const auto cStart = std::chrono::steady_clock::now();
    acAction();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cStart);
}
} // namespace

TEST_CASE("Network: the host's listener and a guest's connection never block the frame", "[HostAuthority][Network]")
{
    HostNetwork network;
    REQUIRE(network.Listen(21640));
    Guest guest;
    REQUIRE(guest.Connect(std::format("127.0.0.1:{}", network.GetPort())));
    const auto cDeadline = std::chrono::steady_clock::now() + 2s;
    while (!guest.IsConnected() && std::chrono::steady_clock::now() < cDeadline)
    {
        network.Update();
        guest.Update();
    }
    REQUIRE(guest.IsConnected());

    // 100 frames of the listener: a sleep in each would take 200 ms.
    CHECK(Timed([&] {
              for (int i = 0; i < 100; ++i)
                  network.Update();
          }) < 50ms);
    // Leaving, with no answer to wake either side: each waited 100 ms for one.
    const uint16_t cPort = network.GetPort();
    CHECK(Timed([&] { network.Close(); }) < 50ms);
    Guest unanswered;
    REQUIRE(unanswered.ConnectByIp(std::format("127.0.0.1:{}", cPort)));
    CHECK(Timed([&] { unanswered.Close(); }) < 50ms);
}
