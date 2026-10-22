#include <Messages/Message.h>

void RequestMessage::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    // We don't read the opcode, the factory will do it
}

void RequestMessage::DeserializeDifferential(Wire::Buffer::Reader& aReader) noexcept
{
}

RequestOpcode RequestMessage::GetOpcode() const noexcept
{
    return m_opcode;
}

void RequestMessage::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    RequestMessage::SerializeRaw(aWriter);
    SerializeRaw(aWriter);
    SerializeDifferential(aWriter);
}

void RequestMessage::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(m_opcode, sizeof(m_opcode) * 8);
}

void RequestMessage::SerializeDifferential(Wire::Buffer::Writer& aWriter) const noexcept
{
}

void NotifyMessage::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    // We don't read the opcode, the factory will do it
}

void NotifyMessage::DeserializeDifferential(Wire::Buffer::Reader& aReader) noexcept
{
}

NotifyOpcode NotifyMessage::GetOpcode() const noexcept
{
    return m_opcode;
}

void NotifyMessage::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    NotifyMessage::SerializeRaw(aWriter);
    SerializeRaw(aWriter);
    SerializeDifferential(aWriter);
}

void NotifyMessage::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(m_opcode, sizeof(m_opcode) * 8);
}

void NotifyMessage::SerializeDifferential(Wire::Buffer::Writer& aWriter) const noexcept
{
}
