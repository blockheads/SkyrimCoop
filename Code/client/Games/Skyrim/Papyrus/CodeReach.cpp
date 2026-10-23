#include <Papyrus/CodeReach.h>

#include <Zydis/Zydis.h>

#include <algorithm>

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
    for (size_t part = 0; part < parts.size(); ++part)
    {
        const Range cPart = parts[part];
        for (uintptr_t at = cPart.Begin; at < cPart.End; at += instruction.length)
        {
            const std::span<const uint8_t> cBytes = acCode.At(at, cPart.End - at);
            if (cBytes.empty() ||
                ZYAN_FAILED(ZydisDecoderDecodeFull(&Decoder(), cBytes.data(), cBytes.size(), &instruction, operands)))
                break;
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
                else if (!cNoteObject(address) && aThroughCells && acModule.Contains(address))
                {
                    const std::optional<uintptr_t> cHeld = acCode.HeldBy(address);
                    if (cHeld && !cNoteObject(*cHeld) && cIsCall && acCode.IsCode(*cHeld))
                        facts.Callees.push_back(*cHeld);
                }
            }
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

Call ClassifyCall(const Facts& acFacts, const std::unordered_map<uintptr_t, Call>& acCalls) noexcept
{
    bool world = false;
    bool controls = false;
    bool screen = acFacts.ReadsScreen;
    for (const uintptr_t cCallee : acFacts.Callees)
        if (const auto it = acCalls.find(cCallee); it != acCalls.end())
            (it->second == Call::kWorld ? world : it->second == Call::kControls ? controls : screen) = true;
    return world ? Call::kWorld : controls ? Call::kControls : screen ? Call::kScreen : Call::kNone;
}
} // namespace NativeReach
