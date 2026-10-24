#pragma once

// Bethesda didn't put it into a namespace

#include "Games/Primitives.h"

struct PlayerInputHandler
{
    virtual ~PlayerInputHandler();

    bool isEnabled;
};

struct PlayerControlsData
{
    // members
    NiPoint2 MoveInputVec; // 00
    NiPoint2 LookInputVec; // 08
    NiPoint2 PrevMoveVec;  // 10
    NiPoint2 PrevLookVec;  // 18
    float unk20;           // 20
    bool bAutoMove;        // 24
    bool bRunning;         // 25
    std::uint8_t unk26;    // 26
    bool fovSlideMode;     // 27
    bool povScriptMode;    // 28
    bool povBeastMode;     // 29
    std::uint8_t unk2A;    // 2A
    std::uint8_t unk2B;    // 2B
    bool remapMode;        // 2C
    std::uint8_t unk2D;    // 2D
    std::uint16_t unk2E;   // 2E
};

struct PlayerControls
{
    static PlayerControls* GetInstance();

    void SetBlockPlayerInput(bool abFlag) { bBlockPlayerInput = abFlag; }

    void SetEnabled(bool abFlag);

    void SetCamSwitch(bool aSet) noexcept;

public:
    char pad0[0x24];                         // AE: 0x20 on SE (CommonLibSSE-NG; ProcessEvent passes this+0x24)
    PlayerControlsData Data;                 // 024
    std::uint32_t pad054;                    // 054
    GameArray<void*> handlers;               // 058
    GameArray<void*> unk070;                 // 070
    GameArray<void*> unk088;                 // 088
    std::uint8_t unk0A0[8];                  // 0A0
    std::uint64_t unk0A8;                    // 0A8
    float unk0B0[8];                         // 0B0
    std::uint32_t unk0D0[10];                // 0D0
    std::uint8_t unk0F8[8];                  // 0F8
    float unk100[20];                        // 100
    GameArray<void*> actionInterestedActor;  // 150
    char actorArrayLock[8];                  // 168
    PlayerInputHandler* pMovementHandler;    // 170
    PlayerInputHandler* pLookHandler;        // 178
    PlayerInputHandler* pSprintHandler;      // 180
    PlayerInputHandler* pReadyWeaponHandler; // 188
    PlayerInputHandler* pAutoMoveHandler;    // 190
    PlayerInputHandler* pToggleRunHandler;   // 198
    PlayerInputHandler* pActivateHandler;    // 1A0
    PlayerInputHandler* pJumpHandler;        // 1A8
    PlayerInputHandler* shoutHandler;        // 1B0
    PlayerInputHandler* attackBlockHandler;  // 1B8
    PlayerInputHandler* runHandler;          // 1C0
    PlayerInputHandler* sneakHandler;        // 1C8
    PlayerInputHandler* togglePOVHandler;    // 1D0
    bool bNotifyingHandlers;                 // 1D8
    bool bBlockPlayerInput;                  // 1D9
    std::uint16_t unk1DA;                    // 1DA
    std::uint32_t unk1DC;                    // 1DC
};

static_assert(offsetof(PlayerControls, bBlockPlayerInput) == 0x1D9);
static_assert(offsetof(PlayerControls, Data) == 0x24);
static_assert(offsetof(PlayerControls, handlers) == 0x58);

// ControlMap (CommonLibSSE-NG, its AE layout): which user events are on; ToggleControls (68545) writes them.
struct BSInputEnableManager
{
    // UserEvents::USER_EVENT_FLAG (CommonLibSSE-NG): the controls ToggleControls takes and gives back.
    enum UserEvent : uint32_t
    {
        kMovement = 1 << 0,
        kLooking = 1 << 1,
        kActivate = 1 << 2,
        kMenu = 1 << 3,
        kConsole = 1 << 4,
        kPOVSwitch = 1 << 5,
        kFighting = 1 << 6,
        kSneaking = 1 << 7,
        kMainFour = 1 << 8,
        kWheelZoom = 1 << 9,
        kJumping = 1 << 10,
        kVATS = 1 << 11,
    };

    static BSInputEnableManager* Get();

    void EnableOtherEvent(int32_t aFlags, bool aEnable, bool aUnk2);

    uint8_t pad0[0x108];
    GameArray<uint32_t> contextStack; // 108, input contexts (UserEvents::INPUT_CONTEXT_ID), the top last
    uint32_t enabledControls;         // 120
    uint32_t storedControls;          // 124
    int8_t textEntryCount;            // 128, keyboard keys map to nothing while above 0
    bool ignoreKeyboardMouse;         // 129, set by a gamepad's connect event
    bool ignoreActivateDisabledEvents; // 12A
};
static_assert(offsetof(BSInputEnableManager, contextStack) == 0x108);
static_assert(offsetof(BSInputEnableManager, enabledControls) == 0x120);
static_assert(offsetof(BSInputEnableManager, textEntryCount) == 0x128);
