#include <Games/Animation/NodeMemory.h>

#include <catch2/catch.hpp>

#include <cstddef>

using namespace NodeMemory;

namespace
{
// Laid out as the engine's (hkReferencedObject's 16 bytes first): hkbEventsFromRangeModifierInternalState and
// hkbDampingModifierInternalState (offsets from their hkClassMembers, read in IDA), and one with an array of structs.
struct RangeState
{
    uint8_t Header[16];
    ArrayView WasActive; // 10: hkArray<hkBool>
};
struct alignas(16) DampingState
{
    uint8_t Header[16];
    float DampedVector[4];     // 10
    float VecErrorSum[4];      // 20
    float VecPreviousError[4]; // 30
    float DampedValue;         // 40
    float ErrorSum;            // 44
    float PreviousError;       // 48
    void* Pointer;             // a pointer: left out
};
struct Expression
{
    int8_t Raised;
    uint8_t Pad[3];
    float Value;
};
struct ExpressionsState
{
    uint8_t Header[16];
    ArrayView Expressions; // 10: hkArray<struct>
};

const Class cRange{32, {{0x10, kArray, kBool}}};
const Class cDamping{80,
                     {{0x10, kVector4},
                      {0x20, kVector4},
                      {0x30, kVector4},
                      {0x40, kReal},
                      {0x44, kReal},
                      {0x48, kReal},
                      {offsetof(DampingState, Pointer), kPointer}}};
const Class cExpression{8, {{0, kEnum, kInt8}, {4, kReal}}};
const Class cExpressions{32, {{0x10, kArray, kStruct, 0, &cExpression}}};

template <class T> std::vector<uint8_t> Bytes(const T& acObject, const Class& acClass)
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    Write(writer, reinterpret_cast<const uint8_t*>(&acObject), acClass);
    return {buffer.GetData(), buffer.GetData() + writer.Size()};
}

template <class T> bool Apply(const std::vector<uint8_t>& acBytes, T& aObject, const Class& acClass)
{
    Wire::Buffer::Reader reader{std::span<const uint8_t>(acBytes)};
    return Read(reader, reinterpret_cast<uint8_t*>(&aObject), acClass);
}
} // namespace

TEST_CASE("A range modifier's last ranges cross to a copy's node of the same ranges", "[node-memory]")
{
    bool owner[3] = {true, false, true};
    bool copy[3] = {false, true, false};
    RangeState from{{}, {reinterpret_cast<uint8_t*>(owner), 3, 3}};
    RangeState to{{}, {reinterpret_cast<uint8_t*>(copy), 3, 3}};
    REQUIRE(Apply(Bytes(from, cRange), to, cRange));
    CHECK((copy[0] && !copy[1] && copy[2]));
    // Another count: a different node, not written.
    bool other[2] = {};
    RangeState wrong{{}, {reinterpret_cast<uint8_t*>(other), 2, 2}};
    CHECK_FALSE(Apply(Bytes(from, cRange), wrong, cRange));
}

TEST_CASE("A damping modifier's values cross exactly; pointers stay the copy's", "[node-memory]")
{
    DampingState from{};
    from.DampedVector[2] = 0.1f;
    from.VecPreviousError[3] = -7.f;
    from.DampedValue = 1.25f;
    from.PreviousError = std::numeric_limits<float>::denorm_min();
    from.Pointer = &from;
    DampingState to{};
    to.Pointer = &to;
    REQUIRE(Apply(Bytes(from, cDamping), to, cDamping));
    CHECK(to.DampedVector[2] == 0.1f);
    CHECK(to.VecPreviousError[3] == -7.f);
    CHECK(to.DampedValue == 1.25f);
    CHECK(to.PreviousError == std::numeric_limits<float>::denorm_min());
    CHECK(to.Pointer == &to);
}

TEST_CASE("An array of structs crosses member by member", "[node-memory]")
{
    Expression owner[2] = {{1, {}, 0.5f}, {0, {}, 2.f}};
    Expression copy[2] = {};
    ExpressionsState from{{}, {reinterpret_cast<uint8_t*>(owner), 2, 2}};
    ExpressionsState to{{}, {reinterpret_cast<uint8_t*>(copy), 2, 2}};
    REQUIRE(Apply(Bytes(from, cExpressions), to, cExpressions));
    CHECK(copy[0].Raised == 1);
    CHECK(copy[1].Value == 2.f);
    // Truncated bytes: refused.
    auto bytes = Bytes(from, cExpressions);
    bytes.pop_back();
    CHECK_FALSE(Apply(bytes, to, cExpressions));
}

TEST_CASE("A state holding a pointer anywhere is not carried", "[node-memory]")
{
    CHECK_FALSE(HasPointers(cRange));
    CHECK_FALSE(HasPointers(cExpressions));
    const Class cTransition{16, {{0, kInt32}, {8, kPointer}}}; // as a state machine's active transition
    const Class cMachine{104, {{0x10, kArray, kStruct, 0, &cTransition}, {0x50, kReal}}};
    CHECK(HasPointers(cMachine));
    CHECK(HasPointers(cDamping)); // its pointer member, here
}
