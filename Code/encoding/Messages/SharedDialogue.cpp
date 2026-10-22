#include <Messages/SharedDialogue.h>

#include <Wire/Serialization.h>

namespace
{
uint32_t ReadId(Wire::Buffer::Reader& aReader) noexcept
{
    return Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
} // namespace

void DialogueStartRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, NpcId);
}

void DialogueStartRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    NpcId = ReadId(aReader);
}

void DialogueOpenedRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, NpcId);
}

void DialogueOpenedRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    NpcId = ReadId(aReader);
}

void DialogueOptionsRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, Step);
    Serialization::WriteBool(aWriter, Choices);
    SerializeDialogueOptions(aWriter, Options);
}

void DialogueOptionsRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Step = ReadId(aReader);
    Choices = Serialization::ReadBool(aReader);
    DeserializeDialogueOptions(aReader, Options);
}

void DialoguePickRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, Step);
    Topic.Serialize(aWriter);
    Info.Serialize(aWriter);
    Serialization::WriteString(aWriter, Text);
}

void DialoguePickRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Step = ReadId(aReader);
    Topic.Deserialize(aReader);
    Info.Deserialize(aReader);
    Text = Serialization::ReadString(aReader);
}

void DialogueCloseRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteBool(aWriter, Ended);
}

void DialogueCloseRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Ended = Serialization::ReadBool(aReader);
}

void DialogueJoinRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
}

void DialogueJoinRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
}

void NotifyDialogueStart::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, NpcId);
}

void NotifyDialogueStart::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    NpcId = ReadId(aReader);
}

void NotifyDialogueOpen::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, NpcId);
}

void NotifyDialogueOpen::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    NpcId = ReadId(aReader);
}

void NotifyDialogueList::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, Step);
    Serialization::WriteBool(aWriter, Choices);
    SerializeDialogueOptions(aWriter, Options);
}

void NotifyDialogueList::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Step = ReadId(aReader);
    Choices = Serialization::ReadBool(aReader);
    DeserializeDialogueOptions(aReader, Options);
}

void NotifyDialoguePicked::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, Step);
    Topic.Serialize(aWriter);
    Info.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, PlayerId);
}

void NotifyDialoguePicked::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Step = ReadId(aReader);
    Topic.Deserialize(aReader);
    Info.Deserialize(aReader);
    PlayerId = ReadId(aReader);
}

void NotifyDialogueClosed::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
}

void NotifyDialogueClosed::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
}

void DialogueExperienceRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, Experience);
}

void DialogueExperienceRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Experience = Serialization::ReadFloat(aReader);
}

void NotifyDialogueExperience::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, Experience);
}

void NotifyDialogueExperience::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Experience = Serialization::ReadFloat(aReader);
}
