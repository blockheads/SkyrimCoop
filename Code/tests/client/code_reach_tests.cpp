// CodeReach: what a native's code addresses and calls, read from x64 bytes (docs/design/cutscenes.md, "Which calls";
// docs/re/systems/papyrus-guest-scripts.md, "Natives on the player"). The code is assembled here at made-up
// addresses; the process is a fake Code.
#include <catch2/catch.hpp>

#include <Papyrus/CodeReach.h>

#include <map>

using namespace NativeReach;

namespace
{
constexpr Range kGame{0x140000000, 0x143000000};
constexpr Range kPlugin{0x7FF800000000, 0x7FF800100000};

constexpr uintptr_t kThePlayer = 0x142F00000;      // a per-player object (PlayerCharacter's singleton)
constexpr uintptr_t kMoviePlayer = 0x142F00100;    // a screen object
constexpr uintptr_t kToggleControls = 0x140C00000; // ControlMap::ToggleControls
constexpr uintptr_t kShakeCamera = 0x140500000;
constexpr uintptr_t kSetStage = 0x140300000;
constexpr uintptr_t kElsewhere = 0x140900000; // some engine function

// x64 assembled into a function at Base.
struct Asm
{
    uintptr_t Base;
    std::vector<uint8_t> Bytes;

    uintptr_t Here() const
    {
        return Base + Bytes.size();
    }
    void Rel32(uint8_t aOpcode, uintptr_t aTarget, std::initializer_list<uint8_t> aPrefix = {})
    {
        Bytes.insert(Bytes.end(), aPrefix);
        Bytes.push_back(aOpcode);
        Disp(aTarget, 4);
    }
    // The displacement to aTarget from the end of an instruction whose remaining bytes are aRest long.
    void Disp(uintptr_t aTarget, size_t aRest)
    {
        const auto cDisp = static_cast<int32_t>(static_cast<int64_t>(aTarget) - static_cast<int64_t>(Here() + aRest));
        for (int i = 0; i < 4; ++i)
            Bytes.push_back(static_cast<uint8_t>(cDisp >> (8 * i)));
    }
    Asm& Call(uintptr_t aTarget)
    {
        return Rel32(0xE8, aTarget), *this;
    }
    Asm& Jmp(uintptr_t aTarget)
    {
        return Rel32(0xE9, aTarget), *this;
    }
    Asm& Je(uintptr_t aTarget)
    {
        return Rel32(0x84, aTarget, {0x0F}), *this;
    }
    // mov rax, [rip+aAddress]
    Asm& Load(uintptr_t aAddress)
    {
        return Rel32(0x05, aAddress, {0x48, 0x8B}), *this;
    }
    // call [rip+aCell]
    Asm& CallThrough(uintptr_t aCell)
    {
        return Rel32(0x15, aCell, {0xFF}), *this;
    }
    Asm& Nop()
    {
        return Bytes.push_back(0x90), *this;
    }
    Asm& Ret()
    {
        return Bytes.push_back(0xC3), *this;
    }
    Range Extent() const
    {
        return {Base, Here()};
    }
};

class FakeCode final : public Code
{
  public:
    void Add(const Asm& acCode)
    {
        m_code[acCode.Base] = acCode.Bytes;
    }
    // An unwind entry: aPart of the function starting at aFunction.
    void AddEntry(Range aPart, uintptr_t aFunction)
    {
        m_entries.push_back({aPart, aFunction});
    }
    void AddCell(uintptr_t aCell, uintptr_t aHeld)
    {
        m_cells[aCell] = aHeld;
    }

    std::span<const uint8_t> At(uintptr_t aAt, size_t aMax) const noexcept override
    {
        for (const auto& [cBase, cBytes] : m_code)
            if (aAt >= cBase && aAt < cBase + cBytes.size())
                return std::span(cBytes).subspan(aAt - cBase, std::min(aMax, cBytes.size() - (aAt - cBase)));
        return {};
    }
    std::optional<Entry> EntryOf(uintptr_t aAddress) const noexcept override
    {
        for (const auto& cEntry : m_entries)
            if (cEntry.Part.Contains(aAddress))
                return cEntry;
        return std::nullopt;
    }
    std::optional<uintptr_t> HeldBy(uintptr_t aCell) const noexcept override
    {
        const auto it = m_cells.find(aCell);
        return it != m_cells.end() ? std::optional(it->second) : std::nullopt;
    }
    bool IsCode(uintptr_t aAddress) const noexcept override
    {
        return kGame.Contains(aAddress) || kPlugin.Contains(aAddress);
    }

  private:
    std::map<uintptr_t, std::vector<uint8_t>> m_code;
    std::vector<Entry> m_entries;
    std::map<uintptr_t, uintptr_t> m_cells;
};

const std::unordered_set<uintptr_t> cPerPlayer{kThePlayer};
const std::unordered_set<uintptr_t> cScreen{kMoviePlayer};
const Objects cObjects{cPerPlayer, cScreen};
const std::unordered_map<uintptr_t, Call> cCalls{
    {kToggleControls, Call::kControls}, {kShakeCamera, Call::kScreen}, {kSetStage, Call::kWorld}};
} // namespace

SCENARIO("Reading a leaf function (no unwind data)", "[reach]")
{
    FakeCode code;
    Asm native{0x140100000};
    native.Load(kThePlayer).Call(kToggleControls).Je(native.Base).Ret().Call(kSetStage); // after the ret: not its code
    code.Add(native);

    const Facts cFacts = Read(native.Base, kGame, false, code, cObjects);
    THEN("it reads the player and calls the controls, and stops at its ret")
    {
        CHECK(cFacts.Reads);
        CHECK(!cFacts.ReadsScreen);
        CHECK(cFacts.Callees == std::vector<uintptr_t>{kToggleControls});
    }
}

SCENARIO("Reading a function over its unwind entries", "[reach]")
{
    FakeCode code;
    Asm body{0x140200000};
    Asm cold{0x140280000}; // MSVC's cold part, chained to the body's entry
    body.Call(kElsewhere).Jmp(cold.Base).Nop().Ret();
    cold.Call(kShakeCamera).Jmp(body.Base + 5);
    code.Add(body);
    code.Add(cold);
    code.AddEntry(body.Extent(), body.Base);
    code.AddEntry(cold.Extent(), body.Base);

    const Facts cFacts = Read(body.Base, kGame, false, code, cObjects);
    THEN("both parts are read; jumps between them are not calls")
    CHECK(cFacts.Callees == std::vector<uintptr_t>{kElsewhere, kShakeCamera});

    AND_WHEN("the code after the body's entry is another function's")
    {
        Asm next{body.Here()};
        next.Call(kSetStage).Ret();
        code.Add(next);
        code.AddEntry(next.Extent(), next.Base);
        THEN("it is not read") CHECK(Read(body.Base, kGame, false, code, cObjects).Callees.size() == 2);
    }

    AND_WHEN("asked for an address inside a function")
    THEN("nothing: not a function's start") CHECK(Read(body.Base + 5, kGame, false, code, cObjects).Callees.empty());
}

SCENARIO("A tail jump out of a function is a call", "[reach]")
{
    FakeCode code;
    Asm thunk{0x140110000};
    thunk.Jmp(kToggleControls);
    code.Add(thunk);
    CHECK(Read(thunk.Base, kGame, false, code, cObjects).Callees == std::vector<uintptr_t>{kToggleControls});
}

SCENARIO("A plugin reaches the engine through cells holding its addresses", "[reach]")
{
    FakeCode code;
    constexpr uintptr_t cControlsCell = kPlugin.Begin + 0x8000; // REL::Relocation of ToggleControls
    constexpr uintptr_t cPlayerCell = kPlugin.Begin + 0x8008;   // of the player singleton
    code.AddCell(cControlsCell, kToggleControls);
    code.AddCell(cPlayerCell, kThePlayer);
    Asm native{kPlugin.Begin + 0x1000};
    native.Load(cPlayerCell).CallThrough(cControlsCell).Ret();
    code.Add(native);

    WHEN("read as the plugin's")
    {
        const Facts cFacts = Read(native.Base, kPlugin, true, code, cObjects);
        THEN("the cells count as the engine's objects and calls")
        {
            CHECK(cFacts.Reads);
            CHECK(cFacts.Callees == std::vector<uintptr_t>{kToggleControls});
        }
    }
    WHEN("read as the engine's (its data is no plugin's cell)")
    {
        const Facts cFacts = Read(native.Base, kPlugin, false, code, cObjects);
        THEN("nothing") CHECK((!cFacts.Reads && cFacts.Callees.empty()));
    }
}

SCENARIO("A native reading the movie player is the screen's", "[reach]")
{
    FakeCode code;
    Asm playBink{0x140120000};
    playBink.Load(kMoviePlayer).Ret();
    code.Add(playBink);
    const Facts cFacts = Read(playBink.Base, kGame, false, code, cObjects);
    CHECK(cFacts.ReadsScreen);
    CHECK(ClassifyCall(cFacts, cCalls) == Call::kScreen);
}

SCENARIO("Which of the party's calls a native makes", "[reach]")
{
    THEN("the world comes first")
    CHECK(ClassifyCall({false, true, {kShakeCamera, kToggleControls, kSetStage}}, cCalls) == Call::kWorld);
    THEN("then the controls")
    CHECK(ClassifyCall({false, false, {kShakeCamera, kToggleControls}}, cCalls) == Call::kControls);
    THEN("then the screen") CHECK(ClassifyCall({false, false, {kElsewhere, kShakeCamera}}, cCalls) == Call::kScreen);
    THEN("none of them") CHECK(ClassifyCall({true, false, {kElsewhere}}, cCalls) == Call::kNone);
}

SCENARIO("Reaching this game's player through calls", "[reach]")
{
    // A chain of functions 1 -> 2 -> ... -> 6; aReader reads the player.
    const auto cChain = [](uintptr_t aReader) {
        std::map<uintptr_t, Facts> facts;
        for (uintptr_t f = 1; f <= 6; ++f)
            facts[f] = Facts{f == aReader, false, f < 6 ? std::vector<uintptr_t>{f + 1, 1} : std::vector<uintptr_t>{}};
        return facts;
    };
    const auto cAll = [](uintptr_t) { return true; };
    const auto cReaches = [&](uintptr_t aReader, auto&& aFollows) {
        auto facts = cChain(aReader);
        return Reaches(1, [&](uintptr_t f) -> const Facts& { return facts[f]; }, aFollows);
    };

    THEN("the native itself") CHECK(cReaches(1, cAll));
    THEN("three calls deep") CHECK(cReaches(4, cAll));
    THEN("not four: deeper is unrelated engine code") CHECK(!cReaches(5, cAll));
    THEN("not through what is not followed (an import, the CRT)")
    CHECK(!cReaches(3, [](uintptr_t f) { return f != 2; }));
    THEN("a cycle back to the native ends") CHECK(!cReaches(0, cAll));
}
