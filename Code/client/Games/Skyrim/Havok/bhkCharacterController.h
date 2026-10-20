#pragma once

// An actor's character controller (CommonLibSSE-NG bhkCharacterController), as far as the party's
// pass-through and the test harness's frozen actors use it.
struct bhkCharacterController
{
    // flags (CommonLibSSE-NG CHARACTER_FLAGS).
    static constexpr uint32_t kHitDamage = 1u << 7;
    static constexpr uint32_t kNotPushable = 1u << 14;
    static constexpr uint32_t kRecordHits = 1u << 21;
    static constexpr uint32_t kNoCharacterCollisions = 1u << 27;

    // 00 and 01 are NiRefObject's (its base); the controller's own start at 02.
    virtual ~bhkCharacterController() = 0;                                         // 00
    virtual void DeleteThis() = 0;                                                 // 01
    virtual void GetPositionImpl(void* apPosition, bool aCenterOffset) const = 0;  // 02
    virtual void SetPositionImpl(const void* apPosition, bool, bool) = 0;          // 03
    virtual void GetTransformImpl(void* apTransform) const = 0;                    // 04
    virtual void SetTransformImpl(const void* apTransform) = 0;                    // 05
    virtual void GetLinearVelocityImpl(void* apVelocity) const = 0;                // 06
    virtual void SetLinearVelocityImpl(const void* apVelocity) = 0;                // 07
    // Its bodies' filter info (CFilter): layer in the low 7 bits, the actor's system group in the high 16.
    virtual void GetCollisionFilterInfo(uint32_t& aFilterInfo) const = 0;          // 08

    uint8_t pad08[0x218 - 0x8];
    uint32_t flags; // 218
};
SKYRIM_STRUCT_ASSERT(offsetof(bhkCharacterController, flags) == 0x218);
