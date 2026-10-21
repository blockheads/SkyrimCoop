#pragma once

#pragma pack(push, 1)

struct BGSSaveLoadManager
{
    virtual ~BGSSaveLoadManager();

    struct SaveData
    {
        void* unk0;           // maybe vtable
        uint32_t someCounter; // 0x8
        uint32_t unkC;
        void* unk10;        // 0x10
        void* someFunction; // 0x18

        uint32_t unk58; // 0x58

        uint32_t flags; // 0x340

        char* saveName; // 0xBB0
    };

    void Save(SaveData* apData);

    struct Struct330
    {
        void* unk0; // 0
        void* data; // 8
    };

    uint8_t pad8[0x330 - 0x8];
    Struct330* struct330; // 330
};

SKYRIM_STRUCT_ASSERT(offsetof(BGSSaveLoadManager::SaveData, someFunction) == 0x18);

#pragma pack(pop)

struct TESForm;

// A form's save data as TESForm::SaveGame writes it (CommonLibSSE-NG BGSSaveFormBuffer.h; checked in IDA).
// The engine owns the data: it grows it on the game heap (36056) and frees it in the destructor (36044).
struct BGSSaveFormBuffer
{
    BGSSaveFormBuffer() noexcept;
    ~BGSSaveFormBuffer() noexcept;
    BGSSaveFormBuffer(const BGSSaveFormBuffer&) = delete;
    BGSSaveFormBuffer& operator=(const BGSSaveFormBuffer&) = delete;

    // The change form's header (36037): what a save writes before the data.
    void SetHeader(uint32_t aFormId, uint32_t aChangeFlags, uint8_t aFormType, uint8_t aVersion) noexcept;
    void Reserve(uint32_t aSize) noexcept;
    // A form id as a network id (mod and base), in place of the save's own index (SaveLoad.cpp).
    void WriteId(uint32_t aId) noexcept;

    void* vtable;          // BGSSaveFormBuffer's, set by the engine's constructor (36035)
    char* buffer;          // 08
    uint32_t capacity;     // 10
    uint32_t position;     // 14: the size written
    uint8_t header[0x10];  // 18: numeric id, change flags, form type, version (SetHeader)
    TESForm* form;         // 28
};
static_assert(sizeof(BGSSaveFormBuffer) == 0x30);
static_assert(offsetof(BGSSaveFormBuffer, position) == 0x14 && offsetof(BGSSaveFormBuffer, form) == 0x28);

// A form's save data as TESForm::LoadGame reads it (CommonLibSSE-NG BGSLoadFormBuffer.h, BGSLoadGameBuffer.h,
// BGSLoadFormData.h; checked in IDA). The data is ours: the engine never frees it.
struct BGSLoadFormBuffer
{
    BGSLoadFormBuffer() noexcept;
    BGSLoadFormBuffer(const BGSLoadFormBuffer&) = delete;
    BGSLoadFormBuffer& operator=(const BGSLoadFormBuffer&) = delete;

    void SetData(const char* apData, uint32_t aSize) noexcept;

    void* vtable;              // BGSLoadFormBuffer's (187480)
    const char* buffer;        // 08: read with no bounds check (LoadDataEndian, 36005)
    void* scrapHeap;           // 10: the data's heap when the engine allocated it; ours: none
    uint32_t capacity;         // 18
    int32_t unk1C;             // 1C
    uint32_t size;             // 20
    uint32_t position;         // 24
    // BGSLoadFormData
    uint32_t formId;           // 28
    uint32_t dataSize;         // 2C
    uint32_t uncompressedSize; // 30
    uint32_t pad34;            // 34
    TESForm* form;             // 38
    uint32_t changeFlags;      // 40
    uint32_t oldChangeFlags;   // 44
    uint16_t flags;            // 48
    uint8_t formInfo;          // 4A
    uint8_t version;           // 4B: what LoadGame reads through GetVersion (35990)
};
static_assert(sizeof(BGSLoadFormBuffer) == 0x50);
static_assert(offsetof(BGSLoadFormBuffer, position) == 0x24 && offsetof(BGSLoadFormBuffer, formId) == 0x28 &&
              offsetof(BGSLoadFormBuffer, form) == 0x38 && offsetof(BGSLoadFormBuffer, changeFlags) == 0x40 &&
              offsetof(BGSLoadFormBuffer, version) == 0x4B);

// The save system's state (CommonLibSSE-NG BGSSaveLoadGame.h).
struct BGSSaveLoadGame
{
    static BGSSaveLoadGame* Get() noexcept;
    // The change form version this build's saves write (35740).
    static uint8_t CurrentVersion() noexcept;

    bool IsSavingOrLoading() const noexcept { return (globalFlags & (kSaveGameLoading | kSaveGameSaving)) != 0; }
    // While set on this thread the engine records no changes (19378, TLS +0x670 bit 0): returns whether it was clear.
    bool BeginLoading() noexcept;
    void EndLoading(bool aWasClear) noexcept;
    // Which of aFlags the form's change record holds (35577): what a save, or its cell's unload, keeps of it.
    bool HasChange(const TESForm& acForm, uint32_t aFlags) const noexcept;

    enum : uint32_t
    {
        kSaveGameLoading = 1 << 1,
        kSaveGameSaving = 1 << 2,
    };

    uint8_t pad0[0x340];
    uint32_t globalFlags; // 340
};
static_assert(offsetof(BGSSaveLoadGame, globalFlags) == 0x340);
