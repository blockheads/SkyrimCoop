#include <Structs/ScriptValue.h>
#include <Wire/Serialization.h>

#include <Overloaded.h>

using Wire::Serialization;

void SerializeScriptValue(Wire::Buffer::Writer& aWriter, const ScriptValue& acValue) noexcept
{
    aWriter.WriteBits(acValue.index(), 8);
    std::visit(Overloaded{
                   [](std::monostate) {},
                   [&aWriter](int32_t aValue) { Serialization::WriteVarInt(aWriter, static_cast<uint32_t>(aValue)); },
                   [&aWriter](float aValue) { Serialization::WriteFloat(aWriter, aValue); },
                   [&aWriter](bool aValue) { Serialization::WriteBool(aWriter, aValue); },
                   [&aWriter](const std::string& acValue) { Serialization::WriteString(aWriter, acValue); },
                   [&aWriter](const ScriptForm& acForm) {
                       Serialization::WriteVarInt(aWriter, acForm.SessionId);
                       acForm.Form.Serialize(aWriter);
                   },
               },
               acValue);
}

bool DeserializeScriptValue(Wire::Buffer::Reader& aReader, ScriptValue& aValue) noexcept
{
    uint64_t index = 0;
    aReader.ReadBits(index, 8);
    switch (index)
    {
    case 0: aValue = std::monostate{}; return true;
    case 1: aValue = static_cast<int32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF); return true;
    case 2: aValue = Serialization::ReadFloat(aReader); return true;
    case 3: aValue = Serialization::ReadBool(aReader); return true;
    case 4: aValue = Serialization::ReadString(aReader); return true;
    case 5:
    {
        ScriptForm form;
        form.SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        form.Form.Deserialize(aReader);
        aValue = form;
        return true;
    }
    default: return false;
    }
}

namespace
{
// A native takes a handful; anything past it is a corrupt message.
constexpr uint64_t kMaxNativeArguments = 16;
} // namespace

void SerializeScriptCall(Wire::Buffer::Writer& aWriter, const ScriptCall& acCall) noexcept
{
    Serialization::WriteString(aWriter, acCall.Class);
    Serialization::WriteString(aWriter, acCall.Function);
    SerializeScriptValue(aWriter, acCall.Self);
    Serialization::WriteVarInt(aWriter, acCall.Arguments.size());
    for (const auto& cArgument : acCall.Arguments)
        SerializeScriptValue(aWriter, cArgument);
}

void DeserializeScriptCall(Wire::Buffer::Reader& aReader, ScriptCall& aCall) noexcept
{
    aCall.Class = Serialization::ReadString(aReader);
    aCall.Function = Serialization::ReadString(aReader);
    if (!DeserializeScriptValue(aReader, aCall.Self))
        aCall.Self = std::monostate{};
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    aCall.Arguments.clear();
    for (uint64_t i = 0; i < cCount && i < kMaxNativeArguments; ++i)
        if (!DeserializeScriptValue(aReader, aCall.Arguments.emplace_back()))
        {
            aCall.Arguments.clear();
            return;
        }
}
