#pragma once

#include "../Opcodes.h"
#include "../ChatMessageTypes.h"

using TiltedPhoques::Serialization;
using TiltedPhoques::String;

struct RequestMessage : TiltedPhoques::AllocatorCompatible
{
    RequestMessage(RequestOpcode aOpcode)
        : m_opcode(aOpcode)
    {
    }

    virtual ~RequestMessage() = default;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    // Serialize values that are not dependent on previous states
    virtual void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    // Serialize values that are dependent on previous states
    virtual void SerializeDifferential(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    // Deserialize values that are dependent on previous states, this function will already be called
    virtual void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept;
    virtual void DeserializeDifferential(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    [[nodiscard]] RequestOpcode GetOpcode() const noexcept;

private:
    RequestOpcode m_opcode;
};

struct NotifyMessage : TiltedPhoques::AllocatorCompatible
{
    NotifyMessage(NotifyOpcode aOpcode)
        : m_opcode(aOpcode)
    {
    }

    virtual ~NotifyMessage() = default;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    virtual void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    virtual void SerializeDifferential(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    virtual void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept;
    virtual void DeserializeDifferential(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    [[nodiscard]] NotifyOpcode GetOpcode() const noexcept;

private:
    NotifyOpcode m_opcode;
};
