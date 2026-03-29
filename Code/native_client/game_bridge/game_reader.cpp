#include "game_reader.h"

#include <unistd.h>
#include <spdlog/spdlog.h>

// TESObjectREFR::position offset (NiPoint3 - 3 floats at 0x54)
static constexpr uint64_t cPosOffset = 0x54;

// TESForm::formID offset
static constexpr uint64_t cFormIdOffset = 0x14;

glm::vec3 GameReader::ReadActorPosition(uint64_t aActorPtr) {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    ssize_t bytesRead = m_mem.Read(pos, sizeof(pos), aActorPtr + cPosOffset);
    if (bytesRead != sizeof(pos)) {
        spdlog::warn("Failed to read actor position at {:#x}", aActorPtr);
        return glm::vec3(0.0f);
    }
    return glm::vec3(pos[0], pos[1], pos[2]);
}

uint32_t GameReader::ReadFormId(uint64_t aFormPtr) {
    uint32_t formId = m_mem.ReadValue<uint32_t>(aFormPtr + cFormIdOffset);
    if (formId == 0) {
        std::string region = m_mem.ValidateAddress(aFormPtr);
        if (region.empty()) {
            spdlog::error("ReadFormId: ptr {:#x} is NOT in any mapped region", aFormPtr);
        } else {
            spdlog::warn("ReadFormId: ptr {:#x} is in region '{}' but formId at +0x14 read as 0", aFormPtr, region);
        }
        // Hex dump the first 0x20 bytes to see what's actually at this pointer
        m_mem.HexDump(aFormPtr, 0x20, "ReadFormId target object");
    }
    return formId;
}

uint32_t GameReader::ReadFormIdDeferred(uint64_t aFormPtr, int aMaxRetries, int aDelayUs) {
    for (int attempt = 0; attempt <= aMaxRetries; attempt++) {
        uint32_t formId = m_mem.ReadValue<uint32_t>(aFormPtr + cFormIdOffset);
        if (formId != 0) {
            if (attempt > 0) {
                spdlog::info("ReadFormIdDeferred: got formId {:#x} at ptr {:#x} on attempt {}",
                    formId, aFormPtr, attempt + 1);
            }
            return formId;
        }
        if (attempt < aMaxRetries) {
            usleep(aDelayUs);
        }
    }
    // All retries failed -- hex dump for diagnostics
    std::string region = m_mem.ValidateAddress(aFormPtr);
    spdlog::warn("ReadFormIdDeferred: ptr {:#x} formId still 0 after {} retries (region: '{}')",
        aFormPtr, aMaxRetries + 1, region.empty() ? "UNMAPPED" : region);
    m_mem.HexDump(aFormPtr, 0x20, "ReadFormIdDeferred exhausted");
    return 0;
}

float GameReader::ReadActorHealth(uint64_t /*aActorPtr*/) {
    // Stub: requires following ActorValueOwner vtable.
    // Will be implemented during service rewrite (Plan 05+).
    return 0.0f;
}
