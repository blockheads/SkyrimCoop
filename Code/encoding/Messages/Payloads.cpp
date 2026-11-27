#include <Messages/Payloads.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

namespace Payloads
{
void ActiveEffects::WriteIds(Wire::Buffer::Writer& aWriter, const std::vector<GameId>& acIds) noexcept
{
    Wire::WriteArchive archive(aWriter);
    archive(Wire::ClampedList(const_cast<std::vector<GameId>&>(acIds), kMaxEffects));
}

void ActiveEffects::ReadIds(Wire::Buffer::Reader& aReader, std::vector<GameId>& aIds) noexcept
{
    Wire::ReadArchive archive(aReader);
    archive(Wire::ClampedList(aIds, kMaxEffects));
}

void ActiveEffects::WriteHeld(Wire::Buffer::Writer& aWriter, const std::vector<struct Held>& acHeld) noexcept
{
    Wire::WriteArchive archive(aWriter);
    archive(Wire::ClampedList(const_cast<std::vector<struct Held>&>(acHeld), kMaxEffects));
}

void ActiveEffects::ReadHeld(Wire::Buffer::Reader& aReader, std::vector<struct Held>& aHeld) noexcept
{
    Wire::ReadArchive archive(aReader);
    archive(Wire::ClampedList(aHeld, kMaxEffects));
}

void RoutedCall::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(To), 2);
    Serialization::WriteVarInt(aWriter, SessionId);
    Serialization::WriteVarInt(aWriter, Source);
    Serialization::WriteVarInt(aWriter, ReplyId);
    SerializeScriptCall(aWriter, Call);
    aWriter.WriteBits(static_cast<uint8_t>(Controls), 2);
}

void RoutedCall::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    uint64_t to = 0;
    aReader.ReadBits(to, 2);
    To = to <= static_cast<uint64_t>(CallTo::kOthers) ? static_cast<CallTo>(to) : CallTo::kOthers;
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Source = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ReplyId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    DeserializeScriptCall(aReader, Call);
    uint64_t controls = 0;
    aReader.ReadBits(controls, 2);
    Controls = controls <= static_cast<uint64_t>(CallControls::kGivesBack) ? static_cast<CallControls>(controls) : CallControls::kNone;
}

void CallEnded::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Source);
    Serialization::WriteVarInt(aWriter, Key);
    SerializeScriptValue(aWriter, Result);
}

void CallEnded::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Source = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    if (!DeserializeScriptValue(aReader, Result))
        Result = std::monostate{};
}

void PlayerNativeResult::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ReplyId);
    SerializeScriptValue(aWriter, Result);
    Serialization::WriteVarInt(aWriter, SessionId);
}

void PlayerNativeResult::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    ReplyId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    if (!DeserializeScriptValue(aReader, Result))
        Result = std::monostate{};
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}

void GraphOpsBatch::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Ops.Serialize(aWriter);
}

void GraphOpsBatch::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    if (!Ops.Deserialize(aReader))
        Ops = {};
}
} // namespace Payloads
