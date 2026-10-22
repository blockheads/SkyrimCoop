#include <Wire/Serialization.h>

namespace Wire
{
    std::string Serialization::ReadString(Buffer::Reader& aReader)
    {
        std::string result;

        const uint16_t len = ReadVarInt(aReader) & 0xFFFF;
        result.resize(len);
        aReader.ReadBytes(std::span(reinterpret_cast<uint8_t*>(result.data()), len));

        return result;
    }

    void Serialization::WriteString(Buffer::Writer& aWriter, const std::string& acString)
    {
        const uint16_t len = acString.size() & 0xFFFF;

        WriteVarInt(aWriter, len);
        aWriter.WriteBytes(std::span(reinterpret_cast<const uint8_t*>(acString.data()), len));
    }

    uint64_t Serialization::ReadVarInt(Buffer::Reader& aReader)
    {
        uint64_t value = 0;
        uint32_t shift = 0;

        do
        {
            uint64_t tmp = 0;
            aReader.ReadBits(tmp, 7);

            value |= (tmp << shift);

            shift += 7;
            
        } while (ReadBool(aReader));

        return value;
    }

    void Serialization::WriteVarInt(Buffer::Writer& aWriter, uint64_t aValue)
    {
        do
        {
            aWriter.WriteBits(aValue, 7);

            aValue >>= 7;

            WriteBool(aWriter, aValue != 0);
        } while (aValue > 0);
    }

    bool Serialization::ReadBool(Buffer::Reader& aReader)
    {
        uint64_t value = 0;
        aReader.ReadBits(value, 1);

        return value == 1;
    }

    void Serialization::WriteBool(Buffer::Writer& aWriter, bool aValue)
    {
        const uint64_t value = aValue ? 1 : 0;
        aWriter.WriteBits(value, 1);
    }

    double Serialization::ReadDouble(Buffer::Reader& aReader)
    {
        double value = 0;
        aReader.ReadBytes(std::span(reinterpret_cast<uint8_t*>(&value), sizeof(value)));

        return value;
    }

    void Serialization::WriteDouble(Buffer::Writer& aWriter, double aValue)
    {
        aWriter.WriteBytes(std::span(reinterpret_cast<const uint8_t*>(&aValue), sizeof(aValue)));
    }

    float Serialization::ReadFloat(Buffer::Reader& aReader)
    {
        float value = 0;
        aReader.ReadBytes(std::span(reinterpret_cast<uint8_t*>(&value), sizeof(value)));

        return value;
    }

    void Serialization::WriteFloat(Buffer::Writer& aWriter, float aValue)
    {
        aWriter.WriteBytes(std::span(reinterpret_cast<const uint8_t*>(&aValue), sizeof(aValue)));
    }
}
