// A game stand-in for scenarios, speaking the session's protocol over the network:
//   coop_peer host --port P --refuse wrong_version|wrong_game|full [--version V] [--game G --skse S]
//       a host that refuses every game asking to join, as build V on Skyrim G with SKSE S; prints
//       "listening P", then "refused <name>" for each
//   coop_peer join --port P [--version V] [--game G --skse S] [--name N]
//       a game on build V (default: this one), Skyrim G and SKSE S (versions as "1.6.1170") asking
//       127.0.0.1:P to join; prints the answer ("accepted", "refused <type>") and exits 0, or exits 1
//       without one in 10 s
#include <chrono>
#include <mutex>
#include <optional>

#include <deque>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <Wire/Buffer.h>
#include <Wire/Serialization.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <network/ENetInterface.h>

using Net::ConnectionId_t;

#include <network/Client.h>
#include <network/Packet.h>
#include <network/Server.h>

#include <BuildVersion.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/RequestMessageFactory.h>
#include <Messages/NotifyMessageFactory.h>

#include <rpmalloc.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

__attribute__((constructor(101))) static void InitRpMalloc()
{
    rpmalloc_initialize();
}

namespace
{
using ResponseType = AuthenticationResponse::ResponseType;

// [packet type byte][message], as the games send it.
template <class TMessage, class TSend> void SendMessage(const TMessage& acMessage, TSend&& aSend)
{
    Wire::Buffer buffer(1 << 16);
    Wire::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8);
    acMessage.Serialize(writer);
    Net::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), static_cast<uint32_t>(writer.Size()));
    aSend(&packet);
}

const char* Name(ResponseType aType)
{
    switch (aType)
    {
    case ResponseType::kAccepted: return "accepted";
    case ResponseType::kWrongVersion: return "wrong_version";
    case ResponseType::kModsMismatch: return "mods_mismatch";
    case ResponseType::kServerFull: return "full";
    case ResponseType::kWrongGame: return "wrong_game";
    default: return "other";
    }
}

struct RefusingHost final : Net::Server
{
    ResponseType Reason{};
    std::string Version;
    GameVersions Game;

    void OnUpdate() override {}
    void OnConnection(ConnectionId_t) override {}
    void OnDisconnection(ConnectionId_t, EDisconnectReason) override {}

    void OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnection) override
    {
        Wire::Buffer::Reader reader(std::span(static_cast<const uint8_t*>(apData), aSize));
        const auto pMessage = RequestMessageFactory{}.Extract(reader);
        if (!pMessage || pMessage->GetOpcode() != kAuthenticationRequest)
            return;

        AuthenticationResponse response;
        response.Type = Reason;
        response.Version = Version.c_str();
        response.Game = Game;
        SendMessage(response, [&](Net::Packet* apPacket) { Send(aConnection, apPacket); });
        Kick(aConnection);
        std::printf("refused %s\n", static_cast<const AuthenticationRequest&>(*pMessage).Username.c_str());
        std::fflush(stdout);
    }
};

struct JoiningGame final : Net::Client
{
    AuthenticationRequest Request;
    std::optional<ResponseType> Answer;

    void OnUpdate() override {}
    void OnDisconnected(EDisconnectReason) override {}

    void OnConnected() override
    {
        SendMessage(Request, [&](Net::Packet* apPacket) { Send(apPacket); });
    }

    void OnConsume(const void* apData, uint32_t aSize) override
    {
        Wire::Buffer::Reader reader(std::span(static_cast<const uint8_t*>(apData), aSize));
        const auto pMessage = NotifyMessageFactory{}.Extract(reader);
        if (pMessage && pMessage->GetOpcode() == kAuthenticationResponse)
            Answer = static_cast<const AuthenticationResponse&>(*pMessage).Type;
    }
};

// "1.6.1170" packed as SKSE packs it.
uint32_t ParseVersion(const std::string& acText)
{
    uint32_t parts[3]{};
    std::sscanf(acText.c_str(), "%u.%u.%u", &parts[0], &parts[1], &parts[2]);
    return GameVersions::Pack(parts[0], parts[1], parts[2]);
}

int Usage()
{
    std::fprintf(stderr, "usage: coop_peer host --port P --refuse wrong_version|wrong_game|full [--version V] [--game G --skse S]\n"
                         "       coop_peer join --port P [--version V] [--game G --skse S] [--name N]\n");
    return 2;
}
} // namespace

int main(int argc, char** argv)
{
    spdlog::set_level(std::getenv("COOP_PEER_DEBUG") ? spdlog::level::debug : spdlog::level::warn);
    if (argc < 2)
        return Usage();
    const std::string cMode = argv[1];
    uint16_t port = 0;
    std::string refuse, version = BuildCommit(), name = "CoopPeer";
    GameVersions game;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const std::string cOption = argv[i];
        if (cOption == "--port")
            port = static_cast<uint16_t>(std::stoi(argv[i + 1]));
        else if (cOption == "--refuse")
            refuse = argv[i + 1];
        else if (cOption == "--version")
            version = argv[i + 1];
        else if (cOption == "--name")
            name = argv[i + 1];
        else if (cOption == "--game")
            game.Runtime = ParseVersion(argv[i + 1]);
        else if (cOption == "--skse")
            game.Skse = ParseVersion(argv[i + 1]);
        else
            return Usage();
    }
    if (port == 0)
        return Usage();

    if (cMode == "host")
    {
        RefusingHost host;
        if (refuse == "wrong_version")
            host.Reason = ResponseType::kWrongVersion;
        else if (refuse == "wrong_game")
            host.Reason = ResponseType::kWrongGame;
        else if (refuse == "full")
            host.Reason = ResponseType::kServerFull;
        else
            return Usage();
        host.Version = version;
        host.Game = game;
        if (!host.Host(port, 60))
            return 1;
        std::printf("listening %u\n", port);
        std::fflush(stdout);
        for (;;)
        {
            host.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    if (cMode == "join")
    {
        JoiningGame joining;
        joining.Request.Version = version.c_str();
        joining.Request.Game = game;
        joining.Request.Username = name.c_str();
        joining.Request.Level = 1;
        if (!joining.ConnectByIp("127.0.0.1:" + std::to_string(port)))
            return 1;
        const auto cDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!joining.Answer && std::chrono::steady_clock::now() < cDeadline)
        {
            joining.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!joining.Answer)
        {
            std::printf("no answer\n");
            return 1;
        }
        if (*joining.Answer == ResponseType::kAccepted)
            std::printf("accepted\n");
        else
            std::printf("refused %s\n", Name(*joining.Answer));
        joining.Close();
        return 0;
    }
    return Usage();
}
