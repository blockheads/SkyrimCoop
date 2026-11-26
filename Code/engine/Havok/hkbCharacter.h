#pragma once

struct CharacterContext;

struct hkbRagdollDriver;

struct hkbCharacter
{
    virtual ~hkbCharacter(){};

    uint8_t pad8[0x30 - 0x8];
    hkbRagdollDriver* ragdollDriver; // 30 (CommonLib-NG, Precision)
    uint8_t pad38[0x58 - 0x38];
    struct hkbBehaviorGraph* behaviorGraph; // 58 (reflection: hkbCharacter members)
    uint8_t pad60[0x80 - 0x60];
    struct hkbEventQueue* eventQueue; // 80: where a graph's raised events wait when its context has no queue
    uint8_t pad88[0xA0 - 0x88];
    CharacterContext* characterContext;
    uint8_t padA8[0xC0 - 0xA8];
};

static_assert(offsetof(hkbCharacter, ragdollDriver) == 0x30);
static_assert(offsetof(hkbCharacter, behaviorGraph) == 0x58);
static_assert(offsetof(hkbCharacter, eventQueue) == 0x80);
static_assert(offsetof(hkbCharacter, characterContext) == 0xA0);
static_assert(sizeof(hkbCharacter) == 0xC0);

// A graph's raised events (hkQueue<hkbEvent>, 24 bytes each; reserve 58323): enqueued at tail, handled from head.
struct hkbEventQueue
{
    void* data;         // 00
    int32_t capacity;   // 08
    int32_t head;       // 0C
    int32_t tail;       // 10
    int32_t count;      // 14
    void* symbolIdMap;  // 18
    bool idsInternal;   // 20
};
static_assert(offsetof(hkbEventQueue, count) == 0x14);
static_assert(offsetof(hkbEventQueue, symbolIdMap) == 0x18);
