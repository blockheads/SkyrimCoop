#pragma once

#include <Wire/Buffer.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// A behaviour node's memory between updates (its internal state object: a state machine's state and transitions, a
// blender's active children, a clip's time), as bytes for a sync point (docs/design/graph-input-stream.md, Sync
// points). Read through Havok 2010's class reflection (hkClass / hkClassMember, coop_hkClass_ctor 57620), so one reader
// serializes any class.
namespace NodeMemory
{
// hkClassMember::Type (Havok 2010), the ones internal states use.
enum Type : uint8_t
{
    kBool = 1,
    kChar = 2,
    kInt8 = 3,
    kUint8 = 4,
    kInt16 = 5,
    kUint16 = 6,
    kInt32 = 7,
    kUint32 = 8,
    kInt64 = 9,
    kUint64 = 10,
    kReal = 11,
    kVector4 = 12,
    kQuaternion = 13,
    kMatrix3 = 14,
    kRotation = 15,
    kQsTransform = 16,
    kMatrix4 = 17,
    kTransform = 18,
    kPointer = 20,
    kArray = 22,
    kEnum = 24,
    kStruct = 25,
    kFlags = 31,
    kHalf = 32,
};

// What a pointer member carries: unknown ones make the class not carried (HasPointers).
enum class Pointer : uint8_t
{
    kUnknown,
    kSkip,   // into the owner's graph, remade by the restore (a running transition's effect): nothing; read as null
    kNested, // an object the state owns (the effect's own state info): Hooks::WriteNested / ReadNested
};

struct Class;
struct Member
{
    uint16_t Offset = 0;
    uint8_t Type = 0;
    uint8_t Subtype = 0;           // an array's element, an enum's or flags' storage
    uint16_t CArraySize = 0;       // an inline array's count (0: one)
    const Class* Struct = nullptr; // a struct's, or an array of structs' element class
    Pointer Kind = Pointer::kUnknown;
};
struct Class
{
    uint32_t Size = 0;
    std::vector<Member> Members; // its own, not its parent hkReferencedObject's
};

// A plain value's bytes, 0 when not plain (a pointer, an array, a struct).
[[nodiscard]] uint32_t PlainSize(uint8_t aType) noexcept;
// Whether the class holds a pointer it cannot carry anywhere: its memory points into its own graph.
[[nodiscard]] bool HasPointers(const Class& acClass) noexcept;

// hkArray's layout (64-bit): data, size, capacity and flags.
struct ArrayView
{
    uint8_t* Data;
    int32_t Size;
    int32_t CapacityAndFlags;
};

// The engine's side: a new state's arrays and the objects a state owns.
struct Hooks
{
    virtual ~Hooks() = default;
    // Storage for aCount elements (hkArrayUtil::_reserve 57048); the reader sets the size.
    [[nodiscard]] virtual bool Reserve(ArrayView& aArray, uint32_t aCount, uint32_t aElementSize) = 0;
    // A kNested member of the object (a struct or array element) at apObject.
    virtual void WriteNested(Wire::Buffer::Writer& aWriter, const uint8_t* acpObject, const Member& acMember) = 0;
    [[nodiscard]] virtual bool ReadNested(Wire::Buffer::Reader& aReader, uint8_t* apObject, const Member& acMember) = 0;
};

// Its plain members, arrays of them (or of structs) and its kNested pointers; other pointers are left out.
void Write(Wire::Buffer::Writer& aWriter, const uint8_t* acpObject, const Class& acClass, Hooks* apHooks = nullptr);
// Onto an object of the same class: false (it may be partly written) if the bytes do not fit. An array of another
// count than the object's is sized by the hooks (a new state's are empty); without them it does not fit.
[[nodiscard]] bool Read(Wire::Buffer::Reader& aReader, uint8_t* apObject, const Class& acClass, Hooks* apHooks = nullptr);
} // namespace NodeMemory
