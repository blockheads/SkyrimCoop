#pragma once

#include "../Opcodes.h"
#include "../ChatMessageTypes.h"
#include <Wire/Buffer.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

struct RequestMessage
{
    RequestMessage(RequestOpcode aOpcode)
        : m_opcode(aOpcode)
    {
    }

    virtual ~RequestMessage() = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    // Serialize values that are not dependent on previous states
    virtual void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept;
    // Serialize values that are dependent on previous states
    virtual void SerializeDifferential(Wire::Buffer::Writer& aWriter) const noexcept;
    // Deserialize values that are dependent on previous states, this function will already be called
    virtual void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept;
    virtual void DeserializeDifferential(Wire::Buffer::Reader& aReader) noexcept;

    [[nodiscard]] RequestOpcode GetOpcode() const noexcept;

private:
    RequestOpcode m_opcode;
};

struct NotifyMessage
{
    NotifyMessage(NotifyOpcode aOpcode)
        : m_opcode(aOpcode)
    {
    }

    virtual ~NotifyMessage() = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    virtual void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept;
    virtual void SerializeDifferential(Wire::Buffer::Writer& aWriter) const noexcept;
    virtual void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept;
    virtual void DeserializeDifferential(Wire::Buffer::Reader& aReader) noexcept;

    [[nodiscard]] NotifyOpcode GetOpcode() const noexcept;

private:
    NotifyOpcode m_opcode;
};

// A message as its own type, from the factory's base pointer.
template <class T, class U> std::unique_ptr<T> CastUnique(std::unique_ptr<U> aMessage) noexcept
{
    return std::unique_ptr<T>(static_cast<T*>(aMessage.release()));
}
