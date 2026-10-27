// CodeReach: what a native's code addresses and calls, read from x64 bytes (docs/design/cutscenes.md, "Which calls";
// docs/re/systems/papyrus-guest-scripts.md, "Natives on the player"). The code is assembled here at made-up
// addresses; the process is a fake Code.
#include <catch2/catch.hpp>

#include <Papyrus/CodeReach.h>

#include <algorithm>
#include <map>
#include <string>

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
constexpr uintptr_t kElsewhere = 0x140900000;
constexpr uintptr_t kApplyArtObject = 0x140366B20; // some engine function
constexpr uintptr_t kHudMessage = 0x14097A5E0;     // SendHUDMessage::ShowHUDMessage
constexpr uintptr_t kEquipObject = 0x1406C9820;    // ActorEquipManager::EquipObject

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

constexpr uintptr_t kPlayerCell = 0x1431874F8; // g_thePlayer (403521) in 1.6.1170, as the natives below load it

const std::unordered_set<uintptr_t> cPerPlayer{kThePlayer, kPlayerCell};
const std::unordered_set<uintptr_t> cScreen{kMoviePlayer};
const std::unordered_set<uintptr_t> cPlayer{kPlayerCell};
const Objects cObjects{cPerPlayer, cScreen, cPlayer};
const Sinks cSinks{.Functions = {{kToggleControls, {{Call::kControls}}},
                                 {kShakeCamera, {{Call::kScreen}}},
                                 {kSetStage, {{Call::kWorld}}},
                                 {kApplyArtObject, {{Call::kShow}}},
                                 {kHudMessage, {{Call::kScreen}}},
                                 {kEquipObject, {{Call::kItem}}}}};

// A native's real code (1.6.1170, read from IDA), at its own address so its RIP-relative operands hold.
Asm Native(uintptr_t aAddress, std::string_view aHex)
{
    Asm code{aAddress};
    for (size_t i = 0; i + 1 < aHex.size(); i += 2)
        code.Bytes.push_back(static_cast<uint8_t>(std::stoi(std::string(aHex.substr(i, 2)), nullptr, 16)));
    return code;
}

// The native read over its unwind entry, as the game reads it.
Facts ReadNative(uintptr_t aAddress, std::string_view aHex)
{
    FakeCode code;
    const Asm cNative = Native(aAddress, aHex);
    code.Add(cNative);
    code.AddEntry(cNative.Extent(), cNative.Base);
    return Read(cNative.Base, kGame, false, code, cObjects);
}
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
    CHECK(ClassifyCall(cFacts, cSinks).Kind == Call::kScreen);
}

SCENARIO("Which of the party's calls a native makes", "[reach]")
{
    THEN("the world comes first")
    CHECK(ClassifyCall({false, true, {kShakeCamera, kToggleControls, kSetStage}}, cSinks).Kind == Call::kWorld);
    THEN("then the controls")
    CHECK(ClassifyCall({false, false, {kShakeCamera, kToggleControls}}, cSinks).Kind == Call::kControls);
    THEN("then the screen")
    CHECK(ClassifyCall({false, false, {kElsewhere, kApplyArtObject, kShakeCamera}}, cSinks).Kind == Call::kScreen);
    THEN("then what a reference shows")
    CHECK(ClassifyCall({false, false, {kElsewhere, kApplyArtObject}}, cSinks).Kind == Call::kShow);
    THEN("none of them") CHECK(ClassifyCall({true, false, {kElsewhere}}, cSinks).Kind == Call::kNone);
    THEN("a notification is the screen's (Debug.Notification)")
    CHECK(ClassifyCall({false, false, {kHudMessage}}, cSinks).Kind == Call::kScreen);
    THEN("an equip showing its HUD message is an item change, not the screen's (Actor.EquipItem)")
    CHECK(ClassifyCall({false, false, {kHudMessage, kEquipObject}}, cSinks).Kind == Call::kItem);
}

// docs/design/party-calls.md, "Which natives are progression"
SCENARIO("A native's virtual calls and writes on the player, followed through its registers", "[reach][dragonborn]")
{
    THEN("Actor.AddPerk: a tail call through its self's vtable (0x7D8)")
    {
        const Facts cFacts = ReadNative(0x1409E7520, "4d8bd04d85c97413498b00498bd14533c0498bca48ffa0d8070000488b01448bc241b902000000488d152aeaf20048ff6010");
        CHECK(cFacts.Virtuals == std::vector<VirtualCall>{{false, 0, 0x7D8, std::nullopt}});
    }
    THEN("Actor.ModActorValue: its self's actor values (+0xB8), slot 0x30, the permanent modifier (0) behind a branch")
    {
        const Facts cFacts = ReadNative(
            0x1409E90F0,
            "48895c240848896c24104889742418574883ec20488bf1498bd9498b09498be88bfae8892da5ff4c63c04181f8a40000007509488d1526d1f200eb34488b05"
            "ddd170014a8b4cc0088b4160c1e80ea8017517f30f105c2450488d8db8000000488b0133d2ff5030eb15488d1500d1f200488b0b448bcf4c8bc6e8b2d2000048"
            "8b5c2430488b6c2438488b7424404883c4205fc3");
        CHECK(cFacts.Virtuals == std::vector<VirtualCall>{{false, 0xB8, 0x30, 0}});
    }
    THEN("Actor.ForceActorValue sets the same permanent modifier; DamageActorValue the damage one (2), after its return")
    {
        const Facts cForce = ReadNative(
            0x1409E81E0,
            "48895c240848896c2410488974241848897c242041564883ec20488be9498bd9498b094d8bf08bf2e8933ca5ff4863f881ffa40000007509488d1531e0f200eb"
            "52488b05e8e07001488b4cf8088b4160c1e80ea8017535498b86b8000000498d8eb80000008bd7ff5008f30f105c2450498d8eb8000000498b86b8000000f30f"
            "5cd8448bc733d2ff5030eb15488d15eddff200488b0b448bce4c8bc5e89fe10000488b5c2430488b6c2438488b742440488b7c24484883c420415ec3");
        CHECK(std::ranges::count(cForce.Virtuals, VirtualCall{false, 0xB8, 0x30, 0}) == 1);
        const Facts cDamage = ReadNative(
            0x1409E7D00,
            "48895c240848896c2410488974241848897c242041564883ec304c8bf1498bf1498b09498bd88beae87341a5ff4863f881ffa40000007517488b0e488d150ee5"
            "f200448bcd4d8bc6e8d3e60000eb68488b05bae57001488b4cf8088b51608bc2c1e80ea8010f85810000000f29742420f30f107424600f543593c6d800c1ea09"
            "f6c20175070f5735a4b30e0183ff187541488b03488bcbff90f002000084c07431488b030f28d633d2488bcbff90200800000f28742420488b5c2440488b6c24"
            "48488b742450488b7c24584883c430415ec3488d8bb80000000f28de488b01448bc7ba02000000ff5030ebc6488b0e488d156ae4f200448bcd4d8bc6e81fe6"
            "0000ebb4");
        CHECK(std::ranges::count(cDamage.Virtuals, VirtualCall{false, 0xB8, 0x30, 2}) == 1);
        CHECK(std::ranges::count(cDamage.Virtuals, VirtualCall{false, 0xB8, 0x30, 0}) == 0);
    }
    THEN("Game.AdvanceSkill: the player's vtable (g_thePlayer), slot 0x7B8")
    {
        const Facts cFacts = ReadNative(
            0x140A0BAF0,
            "48895c24084889742410574881ec30010000488bd9498bf1498b098bfae88e03a3ff8bd0448d40fa4183f81176204c8b0e4c8d05404af100ba00010000488d4c"
            "2430e8c9ed77ff488d542430eb35f30f109424600100000f57c00f2fd0761d488b0da2b977024533c9c744242000000000488b01ff90b8070000eb19488d151d"
            "4af100488b0341b902000000448bc7488bcbff50104c8d9c2430010000498b5b10498b7318498be35fc3");
        CHECK(cFacts.Virtuals == std::vector<VirtualCall>{{true, 0, 0x7B8, std::nullopt}});
    }
    THEN("Game.AddPerkPoints: a write of the player's perk points (+0xB09), no call")
    {
        FakeCode code;
        code.Add(Native(0x140A0BC20, "488b15d1b8770241b8ff0000000fb682090b0000413bc073184585c97e134103c1413bc00fb6c8410f4fc8888a090b0000c3"));
        const Facts cFacts = Read(0x140A0BC20, kGame, false, code, cObjects);
        CHECK(cFacts.PlayerWrites == std::vector<uint32_t>{0xB09});
        CHECK(cFacts.Virtuals.empty());
    }
}

SCENARIO("A getter of where a reference is: a question", "[reach][route]")
{
    Sinks sinks = cSinks;
    sinks.SelfFields[0x54] = {{Call::kQuestion}};
    THEN("ObjectReference.GetPositionX reads its self's position, calling nothing")
    {
        FakeCode code;
        code.Add(Native(0x140A2E000, "f3410f104054c3")); // movss xmm0, [r8+54h]; ret
        const Facts cFacts = Read(0x140A2E000, kGame, false, code, cObjects);
        CHECK(cFacts.SelfReads == std::vector<uint32_t>{0x54});
        CHECK(ClassifyCall(cFacts, sinks).Kind == Call::kQuestion);
    }
    THEN("a native that also calls something is no getter (a move reading where it is)")
    CHECK(ClassifyCall({.Callees = {kElsewhere}, .SelfReads = {0x54}}, sinks).Kind == Call::kNone);
}

SCENARIO("A player's progression, read from the engine functions a native ends in", "[reach][dragonborn]")
{
    constexpr uintptr_t cAddPerk = 0x140748F20;       // PlayerCharacter::AddPerk, the player's vtable at 0x7D8
    constexpr uintptr_t cModActorValue = 0x1406B2770; // ActorValueOwner::ModActorValue
    constexpr uintptr_t cAddSpell = 0x1406C12A0;
    constexpr Reach cReward{Call::kProgress, {.AnyScript = true}};
    constexpr Reach cStoryOnCopy{Call::kProgress, {.OnCopy = true}};
    Sinks sinks = cSinks;
    sinks.Functions[cAddPerk] = {cReward};
    sinks.Functions[cModActorValue] = {cReward, 0};
    sinks.Functions[cAddSpell] = {cStoryOnCopy};
    sinks.PlayerFields[0xB09] = {cReward};
    // The player's vtables: +0 slot 0x7D8 is AddPerk, +0xB8 slot 0x30 ModActorValue.
    const auto cResolve = [&](const VirtualCall& acCall) -> std::optional<uintptr_t> {
        if (acCall.Offset == 0 && acCall.Slot == 0x7D8)
            return cAddPerk;
        if (acCall.Offset == 0xB8 && acCall.Slot == 0x30)
            return cModActorValue;
        return std::nullopt;
    };
    const auto cOf = [&](const Facts& acFacts) { return ClassifyCall(acFacts, sinks, cResolve); };

    THEN("a virtual call resolved to a reward")
    CHECK(cOf({.Virtuals = {{false, 0, 0x7D8, std::nullopt}}}) == cReward);
    THEN("a direct call: the story's, kept on a copy") CHECK(cOf({.Callees = {cAddSpell}}) == cStoryOnCopy);
    THEN("a write of the player's field") CHECK(cOf({.PlayerWrites = {0xB09}}) == cReward);
    THEN("the permanent modifier only: damage (2) or an unknown modifier is none")
    {
        CHECK(cOf({.Virtuals = {{false, 0xB8, 0x30, 0}}}) == cReward);
        CHECK(cOf({.Virtuals = {{false, 0xB8, 0x30, 2}}}).Kind == Call::kNone);
        CHECK(cOf({.Virtuals = {{false, 0xB8, 0x30, std::nullopt}}}).Kind == Call::kNone);
        CHECK(cOf({.Callees = {cModActorValue}}).Kind == Call::kNone);
    }
    THEN("a call no vtable resolves is none") CHECK(cOf({.Virtuals = {{false, 0, 0x10, std::nullopt}}}).Kind == Call::kNone);
    THEN("progression comes before the world") CHECK(cOf({.Callees = {kSetStage, cAddSpell}}) == cStoryOnCopy);
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
