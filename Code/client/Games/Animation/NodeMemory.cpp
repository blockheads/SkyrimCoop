#include "NodeMemory.h"

#include <Wire/Serialization.h>
#include <cstring>

namespace NodeMemory
{
uint32_t PlainSize(uint8_t aType) noexcept
{
    switch (aType)
    {
    case kBool:
    case kChar:
    case kInt8:
    case kUint8:
        return 1;
    case kInt16:
    case kUint16:
    case kHalf:
        return 2;
    case kInt32:
    case kUint32:
    case kReal:
        return 4;
    case kInt64:
    case kUint64:
        return 8;
    case kVector4:
    case kQuaternion:
        return 16;
    case kMatrix3:
    case kRotation:
        return 48;
    case kQsTransform:
        return 48;
    case kMatrix4:
    case kTransform:
        return 64;
    default:
        return 0;
    }
}

bool HasPointers(const Class& acClass) noexcept
{
    for (const auto& cMember : acClass.Members)
    {
        if ((cMember.Type == kPointer && cMember.Kind == Pointer::kUnknown) || (cMember.Type == kArray && cMember.Subtype == kPointer))
            return true;
        if (cMember.Type == kPointer)
            continue;
        const bool cStruct = cMember.Type == kStruct || (cMember.Type == kArray && cMember.Subtype == kStruct);
        if (cStruct && (!cMember.Struct || HasPointers(*cMember.Struct)))
            return true;
        if (!cStruct && cMember.Type != kArray &&
            !PlainSize(cMember.Type == kEnum || cMember.Type == kFlags ? cMember.Subtype : cMember.Type))
            return true; // a type it does not read
        if (cMember.Type == kArray && !cStruct && !PlainSize(cMember.Subtype))
            return true;
    }
    return false;
}

namespace
{
// An enum's or flags' storage is its subtype's.
uint32_t ValueSize(uint8_t aType, uint8_t aSubtype) noexcept
{
    return aType == kEnum || aType == kFlags ? PlainSize(aSubtype) : PlainSize(aType);
}

// One element of an array or one inline value: plain, or a struct's members.
void WriteValue(Wire::Buffer::Writer& aWriter, const uint8_t* acpValue, uint8_t aType, uint8_t aSubtype,
                const Class* apStruct, Hooks* apHooks)
{
    if (aType == kStruct && apStruct)
        Write(aWriter, acpValue, *apStruct, apHooks);
    else if (const uint32_t cSize = ValueSize(aType, aSubtype))
        aWriter.WriteBytes({acpValue, cSize});
}

bool ReadValue(Wire::Buffer::Reader& aReader, uint8_t* apValue, uint8_t aType, uint8_t aSubtype, const Class* apStruct,
               Hooks* apHooks)
{
    if (aType == kStruct && apStruct)
        return Read(aReader, apValue, *apStruct, apHooks);
    const uint32_t cSize = ValueSize(aType, aSubtype);
    return !cSize || aReader.ReadBytes({apValue, cSize});
}

uint32_t ElementSize(const Member& acMember) noexcept
{
    return acMember.Subtype == kStruct ? (acMember.Struct ? acMember.Struct->Size : 0) : ValueSize(acMember.Subtype, 0);
}

uint32_t Stride(const Member& acMember) noexcept
{
    return acMember.Type == kStruct ? (acMember.Struct ? acMember.Struct->Size : 0) : ValueSize(acMember.Type, acMember.Subtype);
}
} // namespace

void Write(Wire::Buffer::Writer& aWriter, const uint8_t* acpObject, const Class& acClass, Hooks* apHooks)
{
    for (const auto& cMember : acClass.Members)
    {
        const uint8_t* pValue = acpObject + cMember.Offset;
        if (cMember.Type == kPointer)
        {
            if (cMember.Kind == Pointer::kNested && apHooks)
                apHooks->WriteNested(aWriter, acpObject, cMember);
            continue;
        }
        if (cMember.Type == kArray)
        {
            const auto* pArray = reinterpret_cast<const ArrayView*>(pValue);
            const uint32_t cElement = ElementSize(cMember);
            const uint32_t cCount = cElement && pArray->Data && pArray->Size > 0 ? uint32_t(pArray->Size) : 0;
            Wire::Serialization::WriteVarInt(aWriter, cCount);
            for (uint32_t i = 0; i < cCount; ++i)
                WriteValue(aWriter, pArray->Data + i * cElement, cMember.Subtype, 0, cMember.Struct, apHooks);
            continue;
        }
        const uint32_t cCount = cMember.CArraySize ? cMember.CArraySize : 1;
        for (uint32_t i = 0; i < cCount; ++i)
            WriteValue(aWriter, pValue + i * Stride(cMember), cMember.Type, cMember.Subtype, cMember.Struct, apHooks);
    }
}

bool Read(Wire::Buffer::Reader& aReader, uint8_t* apObject, const Class& acClass, Hooks* apHooks)
{
    for (const auto& cMember : acClass.Members)
    {
        uint8_t* pValue = apObject + cMember.Offset;
        if (cMember.Type == kPointer)
        {
            if (cMember.Kind == Pointer::kNested && !(apHooks && apHooks->ReadNested(aReader, apObject, cMember)))
                return false;
            if (cMember.Kind == Pointer::kSkip)
                std::memset(pValue, 0, sizeof(void*));
            continue;
        }
        if (cMember.Type == kArray)
        {
            auto* pArray = reinterpret_cast<ArrayView*>(pValue);
            const uint32_t cElement = ElementSize(cMember);
            uint64_t count = 0;
            if (!Wire::Serialization::TryReadVarInt(aReader, count) || count > (1u << 20) || (count && !cElement))
                return false;
            const uint32_t cHave = pArray->Data && pArray->Size > 0 ? uint32_t(pArray->Size) : 0;
            if (count != cHave)
            {
                if (!apHooks || !apHooks->Reserve(*pArray, uint32_t(count), cElement))
                    return false;
                pArray->Size = int32_t(count);
            }
            for (uint32_t i = 0; i < count; ++i)
                if (!ReadValue(aReader, pArray->Data + i * cElement, cMember.Subtype, 0, cMember.Struct, apHooks))
                    return false;
            continue;
        }
        const uint32_t cCount = cMember.CArraySize ? cMember.CArraySize : 1;
        for (uint32_t i = 0; i < cCount; ++i)
            if (!ReadValue(aReader, pValue + i * Stride(cMember), cMember.Type, cMember.Subtype, cMember.Struct, apHooks))
                return false;
    }
    return true;
}
} // namespace NodeMemory
