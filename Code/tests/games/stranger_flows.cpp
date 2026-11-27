// A connection that never joined sends crafted bytes (HostNetwork's gate, the decoders' bounds, the snappy cap): the
// host drops them and its session goes on. Hand-written: a wire bug with no engine path.
#include "Party.h"
#include "Fixtures.h"

#include <Messages/AuthenticationRequest.h>
#include <Messages/NotifyMessageFactory.h>
#include <network/Client.h>
#include <network/ENetInterface.h>
#include <network/Packet.h>
#include <Wire/Serialization.h>

#include <snappy.h>

namespace
{
// Bytes sent as they are, the packet type first.
struct RawPacket final : Net::Packet
{
    explicit RawPacket(std::vector<uint8_t>& aBytes)
    {
        m_pData = reinterpret_cast<char*>(aBytes.data());
        m_size = static_cast<uint32_t>(aBytes.size());
    }
    ~RawPacket() override { m_pData = nullptr; }
};

// A game that connects and never joins: it sends what it is given and notes the host's answer to a join.
struct Stranger final : Net::Client
{
    bool Answered = false;
    bool Left = false;

    void OnUpdate() override {}
    void OnConnected() override {}
    void OnDisconnected(EDisconnectReason) override { Left = true; }
    void OnConsume(const void* apData, uint32_t aSize) override
    {
        Wire::Buffer::Reader reader(std::span(static_cast<const uint8_t*>(apData), aSize));
        const auto pMessage = NotifyMessageFactory{}.Extract(reader);
        Answered |= pMessage && pMessage->GetOpcode() == kAuthenticationResponse;
    }

    void SendRaw(uint8_t aType, std::vector<uint8_t> aBytes)
    {
        aBytes.insert(aBytes.begin(), aType);
        RawPacket packet(aBytes);
        Send(&packet);
    }
};

// [opcode][varints]: a message as any sender could craft it.
std::vector<uint8_t> Crafted(uint8_t aOpcode, std::initializer_list<uint64_t> aVarInts)
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    writer.WriteBits(aOpcode, 8);
    for (const uint64_t cValue : aVarInts)
        Wire::Serialization::WriteVarInt(writer, cValue);
    return {buffer.GetData(), buffer.GetData() + writer.Size()};
}

std::vector<uint8_t> Written(const RequestMessage& acMessage)
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    acMessage.Serialize(writer);
    return {buffer.GetData(), buffer.GetData() + writer.Size()};
}

// Snappy's header alone: a varint of the size it claims.
std::vector<uint8_t> SnappyClaiming(uint64_t aSize)
{
    std::vector<uint8_t> bytes;
    do
    {
        bytes.push_back(static_cast<uint8_t>((aSize & 0x7F) | (aSize > 0x7F ? 0x80 : 0)));
        aSize >>= 7;
    } while (aSize);
    return bytes;
}
} // namespace

TEST_CASE("A compressed payload is unpacked only when its header claims at most the cap", "[network]")
{
    const std::string cMessage(1000, 'a');
    std::string compressed;
    snappy::Compress(cMessage.data(), cMessage.size(), &compressed);
    std::string out;
    CHECK(Net::Uncompress(compressed.data(), compressed.size(), out));
    CHECK(out == cMessage);

    for (const uint64_t cClaim : {uint64_t{Net::kMaxUncompressed} + 1, uint64_t{0xFFFFFFFF}})
    {
        const auto cBytes = SnappyClaiming(cClaim);
        CHECK_FALSE(Net::Uncompress(cBytes.data(), cBytes.size(), out));
    }
    const auto cBroken = SnappyClaiming(100);
    CHECK_FALSE(Net::Uncompress(cBroken.data(), cBroken.size(), out));
}

SCENARIO("A connection that never joined sends crafted messages: the host drops them and plays on", "[session][network]")
{
    Party party(1, SmokeWorld);
    party.Agree("each game has the other's player");

    Stranger stranger;
    REQUIRE(stranger.Connect(std::format("127.0.0.1:{}", party.Host().GetHostService().GetNetwork()->GetPort())));
    party.Until([&] {
        stranger.Update();
        return stranger.IsConnected();
    }, "the stranger is connected");

    // A count of 2^40 (it looped for ever), a message only a player may send, a header claiming 1 GiB; then a join.
    stranger.SendRaw(Net::kPayload, Crafted(kReferencesMoveRequest, {7, uint64_t{1} << 40}));
    stranger.SendRaw(Net::kPayload, Crafted(kSendChatMessageRequest, {2, 'H', 'i'}));
    stranger.SendRaw(Net::kCompressedPayload, SnappyClaiming(uint64_t{1} << 30));
    stranger.SendRaw(Net::kPayload, Written(AuthenticationRequest{}));
    // One reliable channel, in order: the host answered the join after the crafted messages.
    party.Until([&] {
        stranger.Update();
        return stranger.Answered || stranger.Left;
    }, "the host answered the stranger's join, after its crafted messages");

    stranger.Close();
    party.Agree("the session goes on: each game still has the other's player");
}
