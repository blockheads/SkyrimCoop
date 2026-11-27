#pragma once

#include "Serialization.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

// A type states its fields once, in wire order, and both archives walk them:
//     template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id, IsLocked, Bits<8>(Level)); }
// A field is written by its type: bool 1 bit; an integer or an enum a varint; float and double their bytes; a string
// WriteString's; std::array each element; std::optional a presence bit then the value; a type with Fields its fields;
// a type with Serialize/Deserialize those. The wrappers below say anything else.
namespace Wire
{
// A field in its low N bits.
template <size_t N, class T> struct BitsField
{
    T& Value;
};
template <size_t N, class T> BitsField<N, T> Bits(T& aValue) noexcept
{
    return {aValue};
}

// An integer written as a varint of TAs (a signed field zero-extended as uint32_t, not sign-extended).
template <class TAs, class T> struct AsField
{
    T& Value;
};
template <class TAs, class T> AsField<TAs, T> As(T& aValue) noexcept
{
    return {aValue};
}

// The elements' own fields, the default for a list.
struct EachField
{
    template <class TArchive, class T> void operator()(TArchive& aArchive, T& aElement) const noexcept { aArchive(aElement); }
};

// A vector or map behind its count, read through ReadCount with at most Max; Each writes one element (a map: its key
// then its value). Clamp: only the first Max are written.
template <class T, class TEach> struct ListField
{
    T& Value;
    uint64_t Max;
    TEach Each;
    bool Clamp;
};
template <class T, class TEach = EachField> ListField<T, TEach> List(T& aValue, uint64_t aMax, TEach aEach = {}) noexcept
{
    return {aValue, aMax, aEach, false};
}
template <class T> ListField<T, EachField> ClampedList(T& aValue, uint64_t aMax) noexcept
{
    return {aValue, aMax, {}, true};
}

struct WriteArchive;
template <class T> constexpr bool kHasFields = requires(T& aValue, WriteArchive& aArchive) { aValue.Fields(aArchive); };
template <class T> constexpr bool kIsArray = false;
template <class T, size_t N> constexpr bool kIsArray<std::array<T, N>> = true;
template <class T, size_t N> constexpr bool kIsArray<T[N]> = true;
template <class T> constexpr bool kIsOptional = false;
template <class T> constexpr bool kIsOptional<std::optional<T>> = true;
template <class T> constexpr bool kIsMap = false;
template <class K, class V> constexpr bool kIsMap<std::map<K, V>> = true;

// The bits an integer, enum or bool is written as.
template <class T> uint64_t ToBits(T aValue) noexcept
{
    if constexpr (std::is_enum_v<T>)
        return static_cast<uint64_t>(static_cast<std::underlying_type_t<T>>(aValue));
    else
        return static_cast<uint64_t>(aValue);
}
template <class T> T FromBits(uint64_t aBits) noexcept
{
    if constexpr (std::is_same_v<T, bool>)
        return aBits != 0;
    else if constexpr (std::is_enum_v<T>)
        return static_cast<T>(static_cast<std::underlying_type_t<T>>(aBits));
    else
        return static_cast<T>(aBits);
}

struct WriteArchive
{
    static constexpr bool kReading = false;

    explicit WriteArchive(Buffer::Writer& aWriter) noexcept : m_writer(aWriter) {}

    template <class... T> void operator()(T&&... aFields) noexcept { (Field(aFields), ...); }

    template <class T> void Field(T& aValue) noexcept
    {
        if constexpr (std::is_same_v<T, bool>)
            Serialization::WriteBool(m_writer, aValue);
        else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
            Serialization::WriteVarInt(m_writer, ToBits(aValue));
        else if constexpr (std::is_same_v<T, float>)
            Serialization::WriteFloat(m_writer, aValue);
        else if constexpr (std::is_same_v<T, double>)
            Serialization::WriteDouble(m_writer, aValue);
        else if constexpr (std::is_same_v<T, std::string>)
            Serialization::WriteString(m_writer, aValue);
        else if constexpr (kIsArray<T>)
            for (auto& element : aValue)
                Field(element);
        else if constexpr (kIsOptional<T>)
        {
            Serialization::WriteBool(m_writer, aValue.has_value());
            if (aValue)
                Field(*aValue);
        }
        else if constexpr (kHasFields<T>)
            aValue.Fields(*this);
        else
            aValue.Serialize(m_writer);
    }
    template <size_t N, class T> void Field(BitsField<N, T>& aField) noexcept { m_writer.WriteBits(ToBits(aField.Value), N); }
    template <class TAs, class T> void Field(AsField<TAs, T>& aField) noexcept
    {
        Serialization::WriteVarInt(m_writer, static_cast<uint64_t>(static_cast<TAs>(aField.Value)));
    }
    template <class T, class TEach> void Field(ListField<T, TEach>& aField) noexcept
    {
        const uint64_t cCount = aField.Clamp && aField.Value.size() > aField.Max ? aField.Max : aField.Value.size();
        Serialization::WriteVarInt(m_writer, cCount);
        uint64_t written = 0;
        for (auto& element : aField.Value)
        {
            if (written++ == cCount)
                break;
            if constexpr (kIsMap<T>)
            {
                auto key = element.first;
                (*this)(key, element.second);
            }
            else
                aField.Each(*this, element);
        }
    }

private:
    Buffer::Writer& m_writer;
};

struct ReadArchive
{
    static constexpr bool kReading = true;

    explicit ReadArchive(Buffer::Reader& aReader) noexcept : m_reader(aReader) {}

    template <class... T> void operator()(T&&... aFields) noexcept { (Field(aFields), ...); }

    [[nodiscard]] Buffer::Reader& Reader() noexcept { return m_reader; }

    template <class T> void Field(T& aValue) noexcept
    {
        if constexpr (std::is_same_v<T, bool>)
            aValue = Serialization::ReadBool(m_reader);
        else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
            aValue = FromBits<T>(Serialization::ReadVarInt(m_reader));
        else if constexpr (std::is_same_v<T, float>)
            aValue = Serialization::ReadFloat(m_reader);
        else if constexpr (std::is_same_v<T, double>)
            aValue = Serialization::ReadDouble(m_reader);
        else if constexpr (std::is_same_v<T, std::string>)
            aValue = Serialization::ReadString(m_reader);
        else if constexpr (kIsArray<T>)
            for (auto& element : aValue)
                Field(element);
        else if constexpr (kIsOptional<T>)
        {
            aValue.reset();
            if (Serialization::ReadBool(m_reader))
                Field(aValue.emplace());
        }
        else if constexpr (kHasFields<T>)
            aValue.Fields(*this);
        else
            aValue.Deserialize(m_reader);
    }
    template <size_t N, class T> void Field(BitsField<N, T>& aField) noexcept
    {
        uint64_t bits = 0;
        m_reader.ReadBits(bits, N);
        aField.Value = FromBits<T>(bits);
    }
    template <class TAs, class T> void Field(AsField<TAs, T>& aField) noexcept
    {
        aField.Value = static_cast<T>(static_cast<TAs>(Serialization::ReadVarInt(m_reader)));
    }
    template <class T, class TEach> void Field(ListField<T, TEach>& aField) noexcept
    {
        const uint64_t cCount = Serialization::ReadCount(m_reader, aField.Max);
        aField.Value.clear();
        if constexpr (kIsMap<T>)
            for (uint64_t i = 0; i < cCount; ++i)
            {
                typename T::key_type key{};
                typename T::mapped_type value{};
                (*this)(key, value);
                aField.Value.insert_or_assign(key, std::move(value));
            }
        else
        {
            aField.Value.resize(cCount);
            for (auto& element : aField.Value)
                aField.Each(*this, element);
        }
    }

private:
    Buffer::Reader& m_reader;
};

// A value through its fields (or its own Serialize/Deserialize).
template <class T> void Write(Buffer::Writer& aWriter, const T& acValue) noexcept
{
    WriteArchive archive(aWriter);
    archive(const_cast<T&>(acValue));
}
template <class T> void Read(Buffer::Reader& aReader, T& aValue) noexcept
{
    ReadArchive archive(aReader);
    archive(aValue);
}
} // namespace Wire

// Serialize and Deserialize for a struct with Fields, for code that writes it by itself.
#define COOP_SERIALIZE_FIELDS                                                                                          \
    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept { Wire::Write(aWriter, *this); }                    \
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept { Wire::Read(aReader, *this); }
