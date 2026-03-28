#pragma once
#include <cstdint>
#include <glm/vec3.hpp>
#include "proc_memory.h"
#include "pointer_table.h"

class GameReader {
public:
    GameReader(ProcMemReader& aMem, PointerTable& aPtrs)
        : m_mem(aMem), m_ptrs(aPtrs) {}

    // Read a value at ptr + offset
    template<typename T>
    T Read(uint64_t aPtr, uint64_t aOffset = 0) {
        return m_mem.ReadValue<T>(aPtr + aOffset);
    }

    // Read a block of bytes
    ssize_t ReadBytes(void* aBuf, size_t aLen, uint64_t aPtr, uint64_t aOffset = 0) {
        return m_mem.Read(aBuf, aLen, aPtr + aOffset);
    }

    // Follow a pointer chain: read ptr at base+offset, then read T at result+finalOffset
    template<typename T>
    T ReadChain(uint64_t aBase, uint64_t aPtrOffset, uint64_t aFinalOffset) {
        uint64_t intermediate = Read<uint64_t>(aBase, aPtrOffset);
        if (intermediate == 0) return T{};
        return Read<T>(intermediate, aFinalOffset);
    }

    // Convenience: read actor position (TESObjectREFR::position at offset 0x54)
    glm::vec3 ReadActorPosition(uint64_t aActorPtr);

    // Convenience: read form ID (TESForm::formID at offset 0x14)
    uint32_t ReadFormId(uint64_t aFormPtr);

    // Convenience: read actor health (via ActorValueOwner)
    // Stubbed for now -- requires vtable traversal, filled in during service rewrite
    float ReadActorHealth(uint64_t aActorPtr);

    // Lookup by formId, returns 0 if not found
    uint64_t LookupPointer(uint32_t aFormId) { return m_ptrs.Lookup(aFormId); }

    ProcMemReader& GetMem() { return m_mem; }
    PointerTable& GetPointers() { return m_ptrs; }

private:
    ProcMemReader& m_mem;
    PointerTable& m_ptrs;
};
