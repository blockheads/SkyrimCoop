#pragma once

#include <Misc/BSString.h>

struct TESObjectREFR;

// A subtitle on screen (CommonLibSSE-NG SubtitleInfo).
struct SubtitleInfo
{
    uint32_t speakerHandle;
    uint32_t pad04;
    BSString text;
    float targetDistance;
    bool forceDisplay;
    uint8_t pad1D;
    uint16_t pad1E;
};
static_assert(sizeof(SubtitleInfo) == 0x20);
static_assert(offsetof(SubtitleInfo, text) == 0x8);

struct SubtitleManager
{
    static SubtitleManager* Get() noexcept;

    uint8_t unk0[0x8];
    GameArray<SubtitleInfo> subtitles; // 08
    BSRecursiveLock lock;              // 20, BSSpinLock
    uint32_t currentSpeakerHandle;     // 28

    // aForceDisplay: shown at any distance, not only within fMaxSubtitleDistance (SubtitleInfo::forceDisplay).
    void ShowSubtitle(TESObjectREFR* apSpeaker, const char* apSubtitleText, TESTopicInfo* apTopicInfo, bool aForceDisplay = false) noexcept;
    void* HideSubtitle(TESObjectREFR* apSpeaker) noexcept;
};

static_assert(offsetof(SubtitleManager, subtitles) == 0x8);
static_assert(offsetof(SubtitleManager, lock) == 0x20);
static_assert(sizeof(SubtitleManager) == 0x30);
