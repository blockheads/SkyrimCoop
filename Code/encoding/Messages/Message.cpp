#include <Messages/Message.h>

void RequestMessage::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // We don't read the opcode, the factory will do it
}

void RequestMessage::DeserializeDifferential(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
}

RequestOpcode RequestMessage::GetOpcode() const noexcept
{
    return m_opcode;
}

void RequestMessage::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    RequestMessage::SerializeRaw(aWriter);
    SerializeRaw(aWriter);
    SerializeDifferential(aWriter);
}

void RequestMessage::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(m_opcode, sizeof(m_opcode) * 8);
}

void RequestMessage::SerializeDifferential(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
}

void NotifyMessage::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // We don't read the opcode, the factory will do it
}

void NotifyMessage::DeserializeDifferential(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
}

NotifyOpcode NotifyMessage::GetOpcode() const noexcept
{
    return m_opcode;
}

void NotifyMessage::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    NotifyMessage::SerializeRaw(aWriter);
    SerializeRaw(aWriter);
    SerializeDifferential(aWriter);
}

void NotifyMessage::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(m_opcode, sizeof(m_opcode) * 8);
}

void NotifyMessage::SerializeDifferential(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
}
