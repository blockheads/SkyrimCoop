#include <Forms/TESTopic.h>

#include <VersionDb.h>

TESTopicInfo* TESTopic::LineFor(TESObjectREFR* apSpeaker, TESObjectREFR* apPlayer) noexcept
{
    using TLineFor = TESTopicInfo*(TESTopic*, bool* apOut, TESObjectREFR* apSpeaker, TESObjectREFR* apPlayer, bool aUnk,
                                   TESTopicInfo* apParentInfo, void* apHistory);
    POINTER_SKYRIMSE(TLineFor, s_lineFor, 25544);
    bool out = false;
    return s_lineFor(this, &out, apSpeaker, apPlayer, false, nullptr, nullptr);
}
