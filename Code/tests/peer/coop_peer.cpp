// A game stand-in for scenarios, speaking the session's protocol over the network:
//   coop_peer host --port P --refuse wrong_version|full [--version V]
//       a host that refuses every game asking to join (as V, for wrong_version); prints "listening P",
//       then "refused <name>" for each
//   coop_peer join --port P [--version V] [--name N]
//       a game on build V (default: this one) asking 127.0.0.1:P to join; prints the answer
//       ("accepted", "refused <type>") and exits 0, or exits 1 without one in 10 s
#include <chrono>
#include <mutex>
#include <optional>

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <ENetInterface.hpp>

using TiltedPhoques::ConnectionId_t;
using TiltedPhoques::String;
using TiltedPhoques::UniquePtr;
using TiltedPhoques::Vector;

#include <Client.hpp>
#include <Packet.hpp>
#include <Server.hpp>

#include <BuildVersion.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>

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
    TiltedPhoques::Buffer buffer(1 << 16);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8);
    acMessage.Serialize(writer);
    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), static_cast<uint32_t>(writer.Size()));
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
    default: return "other";
    }
}

struct RefusingHost final : TiltedPhoques::Server
{
    ResponseType Reason{};
    std::string Version;

    void OnUpdate() override {}
    void OnConnection(ConnectionId_t) override {}
    void OnDisconnection(ConnectionId_t, EDisconnectReason) override {}

    void OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnection) override
    {
        TiltedPhoques::ViewBuffer buffer(static_cast<uint8_t*>(const_cast<void*>(apData)), aSize);
        TiltedPhoques::Buffer::Reader reader(&buffer);
        const auto pMessage = ClientMessageFactory{}.Extract(reader);
        if (!pMessage || pMessage->GetOpcode() != kAuthenticationRequest)
            return;

        AuthenticationResponse response;
        response.Type = Reason;
        response.Version = Version.c_str();
        SendMessage(response, [&](TiltedPhoques::Packet* apPacket) { Send(aConnection, apPacket); });
        Kick(aConnection);
        std::printf("refused %s\n", static_cast<const AuthenticationRequest&>(*pMessage).Username.c_str());
        std::fflush(stdout);
    }
};

struct JoiningGame final : TiltedPhoques::Client
{
    AuthenticationRequest Request;
    std::optional<ResponseType> Answer;

    void OnUpdate() override {}
    void OnDisconnected(EDisconnectReason) override {}

    void OnConnected() override
    {
        SendMessage(Request, [&](TiltedPhoques::Packet* apPacket) { Send(apPacket); });
    }

    void OnConsume(const void* apData, uint32_t aSize) override
    {
        TiltedPhoques::ViewBuffer buffer(static_cast<uint8_t*>(const_cast<void*>(apData)), aSize);
        TiltedPhoques::Buffer::Reader reader(&buffer);
        const auto pMessage = ServerMessageFactory{}.Extract(reader);
        if (pMessage && pMessage->GetOpcode() == kAuthenticationResponse)
            Answer = static_cast<const AuthenticationResponse&>(*pMessage).Type;
    }
};

int Usage()
{
    std::fprintf(stderr, "usage: coop_peer host --port P --refuse wrong_version|full [--version V]\n"
                         "       coop_peer join --port P [--version V] [--name N]\n");
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
        else if (refuse == "full")
            host.Reason = ResponseType::kServerFull;
        else
            return Usage();
        host.Version = version;
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
        JoiningGame game;
        game.Request.Version = version.c_str();
        game.Request.Username = name.c_str();
        game.Request.Level = 1;
        if (!game.ConnectByIp("127.0.0.1:" + std::to_string(port)))
            return 1;
        const auto cDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!game.Answer && std::chrono::steady_clock::now() < cDeadline)
        {
            game.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!game.Answer)
        {
            std::printf("no answer\n");
            return 1;
        }
        if (*game.Answer == ResponseType::kAccepted)
            std::printf("accepted\n");
        else
            std::printf("refused %s\n", Name(*game.Answer));
        game.Close();
        return 0;
    }
    return Usage();
}
