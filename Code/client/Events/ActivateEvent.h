#pragma once

#include <cstdint>

// Raw SKSE pointer fields are only accessible in code compiled with SKSE headers (HAS_DIRECTXTK).
// Services must use the pre-extracted FormId fields below instead.
#ifdef HAS_DIRECTXTK
struct TESObjectREFR;
struct Actor;
struct TESBoundObject;
#endif

/**
 * @brief In-game activation (i.e. interacting with an object).
 *
 * Raw pointer fields (pObject, pActivator, pObjectToGet, PreActivationOpenState) are only
 * available when HAS_DIRECTXTK is defined. Services must use ObjectFormId / ActivatorFormId
 * which are pre-extracted at the dispatch site (Games/Skyrim hook layer).
 */
struct ActivateEvent
{
#ifdef HAS_DIRECTXTK
    ActivateEvent(TESObjectREFR* apObject, Actor* apActivator, TESBoundObject* apObjectToGet,
                  int32_t aCount, bool aDefaultProcessing, uint8_t aUnk1,
                  TESObjectREFR::OpenState aPreActivationOpenState, bool aActivateFlag = false)
        : pObject(apObject)
        , pActivator(apActivator)
        , pObjectToGet(apObjectToGet)
        , Count(aCount)
        , DefaultProcessing(aDefaultProcessing)
        , Unk1(aUnk1)
        , PreActivationOpenState(aPreActivationOpenState)
        , ActivateFlag(aActivateFlag)
    {
    }

    TESObjectREFR* pObject{nullptr};
    Actor* pActivator{nullptr};
    TESBoundObject* pObjectToGet{nullptr};
    int32_t Count{0};
    bool DefaultProcessing{false};
    uint8_t Unk1{0};
    TESObjectREFR::OpenState PreActivationOpenState{};
    bool ActivateFlag{false};
#else
    ActivateEvent(int32_t aCount, bool aDefaultProcessing, bool aActivateFlag = false)
        : Count(aCount), DefaultProcessing(aDefaultProcessing), ActivateFlag(aActivateFlag) {}

    int32_t Count{0};
    bool DefaultProcessing{false};
    bool ActivateFlag{false};
#endif

    // Pre-extracted primitives: set by the dispatch site (Games/Skyrim hook layer).
    // Services must use these fields; the SKSE pointer fields above are hook-only.
    uint32_t ObjectFormId{0};
    uint32_t ActivatorFormId{0};
    uint32_t ObjectParentCellFormId{0};
    // TESObjectREFR::OpenState cast to uint8_t for SKSE-free service access.
    uint8_t PreActivationOpenStateValue{0};
};
