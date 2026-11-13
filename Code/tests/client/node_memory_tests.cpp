#include <Games/Animation/NodeMemory.h>
#include <Wire/Serialization.h>

#include <catch2/catch.hpp>

#include <cstddef>
#include <cstring>
#include <memory>

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

TEST_CASE("A state holding a pointer it cannot carry is not carried", "[node-memory]")
{
    CHECK_FALSE(HasPointers(cRange));
    CHECK_FALSE(HasPointers(cExpressions));
    const Class cTransition{16, {{0, kInt32}, {8, kPointer}}};
    const Class cMachine{104, {{0x10, kArray, kStruct, 0, &cTransition}, {0x50, kReal}}};
    CHECK(HasPointers(cMachine));
    CHECK(HasPointers(cDamping)); // its pointer member, here
    // A running transition's: its effect remade by the restore, its effect's state carried.
    const Class cKnown{16, {{0, kPointer, 0, 0, nullptr, Pointer::kSkip}, {8, kPointer, kStruct, 0, nullptr, Pointer::kNested}}};
    CHECK_FALSE(HasPointers(Class{104, {{0x10, kArray, kStruct, 0, &cKnown}}}));
}

namespace
{
// hkArrayUtil::_reserve's part, on the heap; nested objects as one int each.
struct TestHooks final : Hooks
{
    std::vector<std::unique_ptr<uint8_t[]>> Blocks;
    std::vector<std::unique_ptr<int32_t>> Nested;
    bool Reserve(ArrayView& aArray, uint32_t aCount, uint32_t aElementSize) override
    {
        Blocks.push_back(std::make_unique<uint8_t[]>(size_t(aCount) * aElementSize));
        aArray.Data = Blocks.back().get();
        aArray.CapacityAndFlags = int32_t(aCount);
        return true;
    }
    void WriteNested(Wire::Buffer::Writer& aWriter, const uint8_t* acpObject, const Member& acMember) override
    {
        const auto* pValue = *reinterpret_cast<const int32_t* const*>(acpObject + acMember.Offset);
        aWriter.WriteBits(pValue ? 1 : 0, 1);
        if (pValue)
            aWriter.WriteBits(uint32_t(*pValue), 32);
    }
    bool ReadNested(Wire::Buffer::Reader& aReader, uint8_t* apObject, const Member& acMember) override
    {
        uint64_t present = 0, value = 0;
        if (!aReader.ReadBits(present, 1) || (present && !aReader.ReadBits(value, 32)))
            return false;
        int32_t* pValue = nullptr;
        if (present)
            pValue = Nested.emplace_back(std::make_unique<int32_t>(int32_t(value))).get();
        std::memcpy(apObject + acMember.Offset, &pValue, sizeof(pValue));
        return true;
    }
};

// As hkbStateMachineActiveTransitionInfo: its effect (into its graph), its effect's state info (its own), a state id.
struct Transition
{
    void* Effect;
    int32_t* EffectState;
    int32_t ToStateId;
    uint8_t Pad[4];
};
struct MachineState
{
    uint8_t Header[16];
    ArrayView Transitions; // 10
    ArrayView Flags;       // 20: hkArray<uint8>
    int32_t CurrentStateId; // 30
};
const Class cTransition{24, {{0, kPointer, 0, 0, nullptr, Pointer::kSkip},
                             {8, kPointer, kStruct, 0, nullptr, Pointer::kNested},
                             {16, kInt32}}};
const Class cMachine{56, {{0x10, kArray, kStruct, 0, &cTransition}, {0x20, kArray, kUint8}, {0x30, kInt32}}};
} // namespace

TEST_CASE("A new state's empty arrays are sized for its owner's; its transitions' effect states cross through the hooks",
          "[node-memory]")
{
    int32_t effectState = 7;
    int ownerEffect = 0;
    Transition transitions[2] = {{&ownerEffect, &effectState, 3, {}}, {&ownerEffect, nullptr, 5, {}}};
    uint8_t flags[3] = {1, 0, 2};
    MachineState owner{{}, {reinterpret_cast<uint8_t*>(transitions), 2, 2}, {flags, 3, 3}, 5};
    TestHooks hooks;
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    Write(writer, reinterpret_cast<const uint8_t*>(&owner), cMachine, &hooks);
    const std::vector<uint8_t> cBytes(buffer.GetData(), buffer.GetData() + writer.Size());

    MachineState copy{{}, {nullptr, 0, int32_t(0x80000000)}, {nullptr, 0, int32_t(0x80000000)}, 0};
    Wire::Buffer::Reader reader{std::span<const uint8_t>(cBytes)};
    REQUIRE(Read(reader, reinterpret_cast<uint8_t*>(&copy), cMachine, &hooks));
    REQUIRE(copy.Transitions.Size == 2);
    const auto* pCopied = reinterpret_cast<const Transition*>(copy.Transitions.Data);
    CHECK(pCopied[0].Effect == nullptr); // the restore makes its own
    REQUIRE(pCopied[0].EffectState);
    CHECK(*pCopied[0].EffectState == 7);
    CHECK(pCopied[1].EffectState == nullptr);
    CHECK(pCopied[1].ToStateId == 5);
    REQUIRE(copy.Flags.Size == 3);
    CHECK(copy.Flags.Data[2] == 2);
    CHECK(copy.CurrentStateId == 5);

    // Without the hooks, another count does not fit.
    MachineState unsized{{}, {nullptr, 0, int32_t(0x80000000)}, {nullptr, 0, int32_t(0x80000000)}, 0};
    Wire::Buffer::Reader again{std::span<const uint8_t>(cBytes)};
    CHECK_FALSE(Read(again, reinterpret_cast<uint8_t*>(&unsized), cMachine));
}

TEST_CASE("An array longer than any state's is refused before its storage is asked for", "[node-memory]")
{
    // A few bytes naming a million elements: no Reserve, so a guest's point cannot make a game allocate for it.
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    Wire::Serialization::WriteVarInt(writer, 1u << 20);
    const std::vector<uint8_t> cBytes(buffer.GetData(), buffer.GetData() + writer.Size());
    TestHooks hooks;
    MachineState copy{{}, {nullptr, 0, int32_t(0x80000000)}, {nullptr, 0, int32_t(0x80000000)}, 0};
    Wire::Buffer::Reader reader{std::span<const uint8_t>(cBytes)};
    CHECK_FALSE(Read(reader, reinterpret_cast<uint8_t*>(&copy), cMachine, &hooks));
    CHECK(hooks.Blocks.empty());
}
