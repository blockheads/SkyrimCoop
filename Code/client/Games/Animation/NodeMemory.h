#pragma once

#include <Wire/Buffer.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// A behaviour node's memory between updates (its internal state object: a range modifier's last ranges, a damping
// modifier's last values), as bytes for a sync point (docs/design/graph-input-stream.md, Sync points). Read through
// Havok 2010's class reflection (hkClass / hkClassMember, coop_hkClass_ctor 57620), so one reader serializes any class.
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

struct Class;
struct Member
{
    uint16_t Offset = 0;
    uint8_t Type = 0;
    uint8_t Subtype = 0;           // an array's element, an enum's or flags' storage
    uint16_t CArraySize = 0;       // an inline array's count (0: one)
    const Class* Struct = nullptr; // a struct's, or an array of structs' element class
};
struct Class
{
    uint32_t Size = 0;
    std::vector<Member> Members; // its own, not its parent hkReferencedObject's
};

// A plain value's bytes, 0 when not plain (a pointer, an array, a struct).
[[nodiscard]] uint32_t PlainSize(uint8_t aType) noexcept;
// Whether the class holds a pointer anywhere (a state machine's active transitions): its memory points into its own
// graph, so it is not carried.
[[nodiscard]] bool HasPointers(const Class& acClass) noexcept;

// hkArray's layout (64-bit): data, size, capacity and flags.
struct ArrayView
{
    uint8_t* Data;
    int32_t Size;
    int32_t CapacityAndFlags;
};

// Its plain members and arrays of them (or of structs); pointers are left out.
void Write(Wire::Buffer::Writer& aWriter, const uint8_t* acpObject, const Class& acClass);
// How Read applies bytes: all of them (a modifier's memory, a clip's); or, for a generator with children, its times
// (reals) only, its structure (integers, bools, arrays: which children are active, in what order) checked equal
// first and nothing written unless it is (another graph's crashed a blender's generate, 60039).
enum class Mode
{
    kAll,
    kCheckStructure, // compares, writes nothing
    kTimes,          // writes the reals only
};
// Onto an object of the same class: false (it may be partly written, but not in kTimes after kCheckStructure) if the
// bytes do not fit, an array's count included (a copy's arrays come sized by its own node, from the same file).
[[nodiscard]] bool Read(Wire::Buffer::Reader& aReader, uint8_t* apObject, const Class& acClass, Mode aMode = Mode::kAll);
} // namespace NodeMemory
