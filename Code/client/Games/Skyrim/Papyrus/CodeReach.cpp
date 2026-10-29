#include <Papyrus/CodeReach.h>

#include <Zydis/Zydis.h>

#include <algorithm>
#include <unordered_map>

namespace NativeReach
{
namespace
{
const ZydisDecoder& Decoder() noexcept
{
    static const ZydisDecoder s_decoder = [] {
        ZydisDecoder decoder;
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        return decoder;
    }();
    return s_decoder;
}

// What a register holds, as far as reading follows it: an object (the native's self, or this game's player) at an
// offset, the vtable found there, or a constant.
struct Held
{
    enum Kind : uint8_t
    {
        kObject,
        kVtable,
        kConstant,
    };
    Kind What;
    bool OnPlayer = false;
    uintptr_t Offset = 0;
    uint64_t Value = 0;
    bool operator==(const Held&) const = default;
};

ZydisRegister Whole(ZydisRegister aRegister) noexcept
{
    return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, aRegister);
}

using HeldMap = std::unordered_map<ZydisRegister, Held>;

// What both paths into an instruction agree a register holds.
void Meet(HeldMap& aInto, const HeldMap& acOther) noexcept
{
    std::erase_if(aInto, [&](const auto& acEntry) {
        const auto it = acOther.find(acEntry.first);
        return it == acOther.end() || it->second != acEntry.second;
    });
}

// The registers each instruction leaves holding something followed, and the virtual calls and player writes it makes.
// One pass forward: a branch's target gets the state at the branch (met with its fall-through's), a return or a jump
// leaves nothing to fall through.
class Registers
{
  public:
    Registers() noexcept
    {
        m_held[ZYDIS_REGISTER_R8] = {Held::kObject}; // a native's self (the VM's calling convention: vm, stack, self)
    }

    // Before the instruction at aAt: what the paths into it agree on.
    void Enter(uintptr_t aAt) noexcept
    {
        const auto it = m_targets.find(aAt);
        if (it != m_targets.end())
        {
            if (m_falls)
                Meet(m_held, it->second);
            else
                m_held = std::move(it->second);
            m_targets.erase(it);
        }
        else if (!m_falls)
            m_held.clear();
        m_falls = true;
    }

    void Follow(const ZydisDecodedInstruction& acInstruction, const ZydisDecodedOperand* apOperands, uintptr_t aAt,
                bool aIsCall, const Objects& acObjects, Facts& aFacts) noexcept
    {
        const ZydisDecodedOperand* pDest = acInstruction.operand_count_visible > 0 ? &apOperands[0] : nullptr;
        const ZydisDecodedOperand* pSource = acInstruction.operand_count_visible > 1 ? &apOperands[1] : nullptr;
        const auto cObjectAt = [this](const ZydisDecodedOperand& acMemory) -> const Held* {
            if (acMemory.type != ZYDIS_OPERAND_TYPE_MEMORY || acMemory.mem.index != ZYDIS_REGISTER_NONE)
                return nullptr;
            const Held* cpBase = Find(acMemory.mem.base);
            return cpBase && cpBase->What == Held::kObject ? cpBase : nullptr;
        };

        if (aIsCall && pDest && pDest->type == ZYDIS_OPERAND_TYPE_MEMORY && pDest->mem.index == ZYDIS_REGISTER_NONE)
            if (const Held* cpTable = Find(pDest->mem.base); cpTable && cpTable->What == Held::kVtable)
            {
                const Held* cpSecond = Find(ZYDIS_REGISTER_RDX);
                aFacts.Virtuals.push_back({cpTable->OnPlayer, cpTable->Offset, static_cast<uint32_t>(pDest->mem.disp.value),
                                           cpSecond && cpSecond->What == Held::kConstant ? std::optional(cpSecond->Value) : std::nullopt});
            }
        if (acInstruction.mnemonic == ZYDIS_MNEMONIC_MOV && pDest)
            if (const Held* cpObject = cObjectAt(*pDest); cpObject && cpObject->OnPlayer)
                aFacts.PlayerWrites.push_back(static_cast<uint32_t>(cpObject->Offset + pDest->mem.disp.value));
        if (acInstruction.mnemonic != ZYDIS_MNEMONIC_LEA)
            for (uint8_t i = 0; i < acInstruction.operand_count_visible; ++i)
                if ((apOperands[i].actions & ZYDIS_OPERAND_ACTION_MASK_READ) && !aIsCall)
                    if (const Held* cpObject = cObjectAt(apOperands[i]); cpObject && !cpObject->OnPlayer)
                        aFacts.SelfReads.push_back(static_cast<uint32_t>(cpObject->Offset + apOperands[i].mem.disp.value));

        std::optional<Held> now;
        if (pDest && pDest->type == ZYDIS_OPERAND_TYPE_REGISTER && pSource)
        {
            if (acInstruction.mnemonic == ZYDIS_MNEMONIC_MOV)
            {
                if (pSource->type == ZYDIS_OPERAND_TYPE_REGISTER)
                {
                    if (const Held* cpHeld = Find(pSource->reg.value))
                        now = *cpHeld;
                }
                else if (pSource->type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                    now = Held{Held::kConstant, false, 0, pSource->imm.value.u};
                else if (pSource->type == ZYDIS_OPERAND_TYPE_MEMORY && pSource->mem.base == ZYDIS_REGISTER_RIP)
                {
                    ZyanU64 address = 0;
                    if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&acInstruction, pSource, aAt, &address)) && acObjects.Player.contains(address))
                        now = Held{Held::kObject, true};
                }
                else if (const Held* cpObject = cObjectAt(*pSource))
                    now = Held{Held::kVtable, cpObject->OnPlayer, cpObject->Offset + static_cast<uintptr_t>(pSource->mem.disp.value)};
            }
            else if (acInstruction.mnemonic == ZYDIS_MNEMONIC_LEA)
            {
                if (const Held* cpObject = cObjectAt(*pSource))
                    now = Held{Held::kObject, cpObject->OnPlayer, cpObject->Offset + static_cast<uintptr_t>(pSource->mem.disp.value)};
            }
            else if (acInstruction.mnemonic == ZYDIS_MNEMONIC_XOR && pSource->type == ZYDIS_OPERAND_TYPE_REGISTER &&
                     Whole(pSource->reg.value) == Whole(pDest->reg.value))
                now = Held{Held::kConstant};
        }

        for (uint8_t i = 0; i < acInstruction.operand_count; ++i)
            if (apOperands[i].type == ZYDIS_OPERAND_TYPE_REGISTER && (apOperands[i].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE))
                m_held.erase(Whole(apOperands[i].reg.value));
        if (aIsCall) // the calling convention's volatile registers
            for (const ZydisRegister cVolatile : {ZYDIS_REGISTER_RAX, ZYDIS_REGISTER_RCX, ZYDIS_REGISTER_RDX, ZYDIS_REGISTER_R8,
                                                  ZYDIS_REGISTER_R9, ZYDIS_REGISTER_R10, ZYDIS_REGISTER_R11})
                m_held.erase(cVolatile);
        if (now)
            m_held[Whole(pDest->reg.value)] = *now;

        // A branch forward: its target starts from what is held here (a call's target is another function).
        const bool cBranch = acInstruction.meta.category == ZYDIS_CATEGORY_COND_BR || acInstruction.meta.category == ZYDIS_CATEGORY_UNCOND_BR;
        ZyanU64 target = 0;
        if (cBranch && pDest && pDest->type == ZYDIS_OPERAND_TYPE_IMMEDIATE && pDest->imm.is_relative &&
            ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&acInstruction, pDest, aAt, &target)) && target > aAt)
        {
            if (const auto [it, cNew] = m_targets.try_emplace(target, m_held); !cNew)
                Meet(it->second, m_held);
        }
        if (acInstruction.mnemonic == ZYDIS_MNEMONIC_RET || acInstruction.mnemonic == ZYDIS_MNEMONIC_INT3 ||
            acInstruction.mnemonic == ZYDIS_MNEMONIC_JMP)
            m_falls = false;
    }

  private:
    const Held* Find(ZydisRegister aRegister) const noexcept
    {
        const auto it = m_held.find(Whole(aRegister));
        return it != m_held.end() ? &it->second : nullptr;
    }

    HeldMap m_held;
    std::unordered_map<uintptr_t, HeldMap> m_targets; // forward branch targets: the state the branches bring
    bool m_falls = true;                              // the previous instruction falls through to the next
};
} // namespace

Facts Read(uintptr_t aFunction, const Range& acModule, bool aThroughCells, const Code& acCode,
           const Objects& acObjects) noexcept
{
    Facts facts;
    const std::optional<Code::Entry> cEntry = acCode.EntryOf(aFunction);
    if (cEntry && cEntry->Function != aFunction)
        return facts; // not a function's start
    // A leaf (no entry) is read until it returns.
    std::vector<Range> parts{{aFunction, cEntry ? cEntry->Part.End : std::min(aFunction + kLeafBytes, acModule.End)}};
    const auto cIsRead = [&parts](uintptr_t aAddress) {
        return std::ranges::any_of(parts, [aAddress](const Range& acPart) { return acPart.Contains(aAddress); });
    };
    // Another entry of this function: added to what is read.
    const auto cAddPart = [&](uintptr_t aAddress) {
        const std::optional<Code::Entry> cPart = cEntry ? acCode.EntryOf(aAddress) : std::nullopt;
        if (!cPart || cPart->Function != aFunction)
            return false;
        if (!cIsRead(cPart->Part.Begin))
            parts.push_back(cPart->Part);
        return true;
    };
    const auto cNoteObject = [&](uintptr_t aAddress) {
        if (acObjects.PerPlayer.contains(aAddress))
            facts.Reads = true;
        else if (acObjects.Screen.contains(aAddress))
            facts.ReadsScreen = true;
        else
            return false;
        return true;
    };

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    Registers registers;
    for (size_t part = 0; part < parts.size(); ++part)
    {
        const Range cPart = parts[part];
        for (uintptr_t at = cPart.Begin; at < cPart.End; at += instruction.length)
        {
            const std::span<const uint8_t> cBytes = acCode.At(at, cPart.End - at);
            if (cBytes.empty() ||
                ZYAN_FAILED(ZydisDecoderDecodeFull(&Decoder(), cBytes.data(), cBytes.size(), &instruction, operands)))
                break;
            registers.Enter(at);
            const bool cIsCall = instruction.meta.category == ZYDIS_CATEGORY_CALL ||
                                 instruction.meta.category == ZYDIS_CATEGORY_UNCOND_BR;
            const bool cIsBranch = cIsCall || instruction.meta.category == ZYDIS_CATEGORY_COND_BR;
            for (uint8_t i = 0; i < instruction.operand_count_visible; ++i)
            {
                const ZydisDecodedOperand& acOperand = operands[i];
                const bool cIsMemory =
                    acOperand.type == ZYDIS_OPERAND_TYPE_MEMORY && acOperand.mem.base == ZYDIS_REGISTER_RIP;
                const bool cIsTarget =
                    acOperand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && acOperand.imm.is_relative && cIsBranch;
                ZyanU64 address = 0;
                if ((!cIsMemory && !cIsTarget) ||
                    ZYAN_FAILED(ZydisCalcAbsoluteAddress(&instruction, &acOperand, at, &address)))
                    continue;
                if (cIsTarget)
                {
                    if (!cIsRead(address) && !cAddPart(address) && cIsCall)
                        facts.Callees.push_back(address);
                }
                else if (instruction.mnemonic == ZYDIS_MNEMONIC_LEA && !cNoteObject(address))
                    facts.Addressed.push_back(address);
                else if (!cNoteObject(address) && aThroughCells && acModule.Contains(address))
                {
                    const std::optional<uintptr_t> cHeld = acCode.HeldBy(address);
                    if (cHeld && !cNoteObject(*cHeld) && cIsCall && acCode.IsCode(*cHeld))
                        facts.Callees.push_back(*cHeld);
                }
            }
            registers.Follow(instruction, operands, at, cIsCall, acObjects, facts);
            const bool cEnds = instruction.mnemonic == ZYDIS_MNEMONIC_RET ||
                               instruction.mnemonic == ZYDIS_MNEMONIC_INT3 ||
                               instruction.mnemonic == ZYDIS_MNEMONIC_JMP;
            if (!cEntry && cEnds)
                break;
        }
        if (cEntry)
            cAddPart(cPart.End); // the next entry, when it is this function's
    }
    return facts;
}

} // namespace NativeReach
