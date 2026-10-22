#include <Messages/SharedDialogue.h>

#include <TiltedCore/Serialization.hpp>

namespace
{
uint32_t ReadId(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    return Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
} // namespace

void DialogueStartRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, NpcId);
}

void DialogueStartRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    NpcId = ReadId(aReader);
}

void DialogueOpenedRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, NpcId);
}

void DialogueOpenedRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    NpcId = ReadId(aReader);
}

void DialogueOptionsRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, Step);
    Serialization::WriteBool(aWriter, Choices);
    SerializeDialogueOptions(aWriter, Options);
}

void DialogueOptionsRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Step = ReadId(aReader);
    Choices = Serialization::ReadBool(aReader);
    DeserializeDialogueOptions(aReader, Options);
}

void DialoguePickRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, Step);
    Topic.Serialize(aWriter);
    Info.Serialize(aWriter);
    Serialization::WriteString(aWriter, Text);
}

void DialoguePickRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Step = ReadId(aReader);
    Topic.Deserialize(aReader);
    Info.Deserialize(aReader);
    Text = Serialization::ReadString(aReader);
}

void DialogueCloseRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteBool(aWriter, Ended);
}

void DialogueCloseRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Ended = Serialization::ReadBool(aReader);
}

void DialogueJoinRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
}

void DialogueJoinRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
}

void NotifyDialogueStart::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, NpcId);
}

void NotifyDialogueStart::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    NpcId = ReadId(aReader);
}

void NotifyDialogueOpen::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, NpcId);
}

void NotifyDialogueOpen::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    NpcId = ReadId(aReader);
}

void NotifyDialogueList::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, Step);
    Serialization::WriteBool(aWriter, Choices);
    SerializeDialogueOptions(aWriter, Options);
}

void NotifyDialogueList::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Step = ReadId(aReader);
    Choices = Serialization::ReadBool(aReader);
    DeserializeDialogueOptions(aReader, Options);
}

void NotifyDialoguePicked::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, Step);
    Topic.Serialize(aWriter);
    Info.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, PlayerId);
}

void NotifyDialoguePicked::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
    Step = ReadId(aReader);
    Topic.Deserialize(aReader);
    Info.Deserialize(aReader);
    PlayerId = ReadId(aReader);
}

void NotifyDialogueClosed::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
}

void NotifyDialogueClosed::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Conversation = ReadId(aReader);
}

void DialogueExperienceRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, Experience);
}

void DialogueExperienceRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Experience = Serialization::ReadFloat(aReader);
}

void NotifyDialogueExperience::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, Experience);
}

void NotifyDialogueExperience::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Experience = Serialization::ReadFloat(aReader);
}
