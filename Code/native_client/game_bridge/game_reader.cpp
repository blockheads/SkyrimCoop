#include "game_reader.h"

#include <spdlog/spdlog.h>

// TESObjectREFR::position offset (NiPoint3 - 3 floats at 0x54)
static constexpr uint64_t cPosOffset = 0x54;

// TESForm::formID offset
static constexpr uint64_t cFormIdOffset = 0x14;

glm::vec3 GameReader::ReadActorPosition(uint64_t aActorPtr) {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    ssize_t bytesRead = m_mem.Read(pos, sizeof(pos), aActorPtr + cPosOffset);
    if (bytesRead != sizeof(pos)) {
        std::string region = m_mem.ValidateAddress(aActorPtr);
        spdlog::warn("Failed to read actor position at {:#x} (region: '{}')",
            aActorPtr, region.empty() ? "UNMAPPED" : region);
        return glm::vec3(0.0f);
    }
    return glm::vec3(pos[0], pos[1], pos[2]);
}

uint32_t GameReader::ReadFormId(uint64_t aFormPtr) {
    uint32_t formId = m_mem.ReadValue<uint32_t>(aFormPtr + cFormIdOffset);
    if (formId == 0) {
        std::string region = m_mem.ValidateAddress(aFormPtr);
        if (region.empty()) {
            spdlog::error("ReadFormId: ptr {:#x} is NOT in any mapped region -- Wine address translation may be needed", aFormPtr);
        } else {
            spdlog::warn("ReadFormId: ptr {:#x} is in region '{}' but formId read as 0 -- possible offset error or uninitialized form", aFormPtr, region);
        }
    }
    return formId;
}

float GameReader::ReadActorHealth(uint64_t /*aActorPtr*/) {
    // Stub: requires following ActorValueOwner vtable.
    // Will be implemented during service rewrite (Plan 05+).
    return 0.0f;
}
