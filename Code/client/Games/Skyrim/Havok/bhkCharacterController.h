#pragma once

// An actor's character controller (CommonLibSSE-NG bhkCharacterController), as far as the party's
// pass-through and the test harness's frozen actors use it.
struct bhkCharacterController
{
    // flags (CommonLibSSE-NG CHARACTER_FLAGS).
    static constexpr uint32_t kHitDamage = 1u << 7;
    static constexpr uint32_t kSupport = 1u << 8;
    static constexpr uint32_t kFarAway = 1u << 18;
    static constexpr uint32_t kNotPushable = 1u << 14;
    static constexpr uint32_t kRecordHits = 1u << 21;
    static constexpr uint32_t kNoCharacterCollisions = 1u << 27;
    // Filter info: the capsule's layer (low 7 bits) and its no-collision bit (AIProcess::EnterFurnitureCollision 40063).
    static constexpr uint32_t kCapsuleLayer = 30;
    static constexpr uint32_t kFilterNoCollision = 1u << 15;
    // state (hkpCharacterStateType): 1 jumping, 2 in the air, 3 climbing, 4 flying, 5 swimming.
    static constexpr int32_t kOnGround = 0;

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
    virtual void Unk09() = 0;                                                      // 09
    virtual void Unk0A() = 0;                                                      // 0A
    virtual void Unk0B() = 0;                                                      // 0B
    virtual void Unk0C() = 0;                                                      // 0C
    virtual void CheckSupportImpl() = 0;                                           // 0D
    virtual void Unk0E() = 0;                                                      // 0E
    virtual void* GetHavokWorld() const = 0;                                       // 0F
    virtual struct hkpRigidBody* GetRigidBody() const = 0;                         // 10: its capsule

    // Havok units: game units / 69.99.
    static constexpr float kHavokToGame = 69.991249f;

    uint8_t pad08[0x90 - 0x8];
    float outVelocity[4]; // 090, the last step's velocity
    uint8_t pad0A0[0x200 - 0xA0];
    int32_t state; // 200, context (1E0) currentState: hkpCharacterStateType, 0 on the ground, 2 in the air
    uint8_t pad204[0x218 - 0x204];
    uint32_t flags; // 218
    uint8_t pad21C[0x258 - 0x21C];
    float scale;       // 258
    uint8_t pad25C[0x260 - 0x25C];
    float actorHeight; // 260
    uint8_t pad264[0x290 - 0x264];
    float radius;      // 290: the capsule's, Havok units
    float height;      // 294
    uint8_t pad298[0x340 - 0x298];
    // A bhkCharProxyController's (this part at its +0x10; vtable ID 240560): its hkpCharacterProxy
    // (bhkCharProxyController::GetLinearVelocityImpl 79145 reads it).
    void* proxy;       // 340
};
static_assert(offsetof(bhkCharacterController, outVelocity) == 0x90);
static_assert(offsetof(bhkCharacterController, state) == 0x200);
static_assert(offsetof(bhkCharacterController, flags) == 0x218);
static_assert(offsetof(bhkCharacterController, actorHeight) == 0x260);
static_assert(offsetof(bhkCharacterController, radius) == 0x290);
static_assert(offsetof(bhkCharacterController, proxy) == 0x340);

// The Havok proxy that walks the capsule: the steepest ground it climbs (no step height: the capsule rides edges).
struct hkpCharacterProxy
{
    uint8_t pad00[0xD8];
    float maxSlopeCosine; // D8
};
static_assert(offsetof(hkpCharacterProxy, maxSlopeCosine) == 0xD8);
