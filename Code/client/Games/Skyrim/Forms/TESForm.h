#pragma once

#include <Components/BaseFormComponent.h>
#include <Misc/BSFixedString.h>
#include <Wire/Buffer.h>

enum class FormType : uint8_t
{
    Armor = 26,
    Book = 27,
    Container = 28,
    Door = 29,
    Furniture = 40,
    Ingredient = 30,
    Weapon = 41,
    Ammo = 42,
    Npc = 43,
    LeveledCharacter = 44,
    Alchemy = 46,
    LeveledItem = 53,
    Cell = 60,
    Reference = 61,
    Character = 62,
    WorldSpace = 71,
    Quest = 77,
    QuestItem = Quest,
    Count = 0x87
};

struct BGSSaveFormBuffer;
struct BGSLoadFormBuffer;

struct TESForm : BaseFormComponent
{
    // A change record's entry (35503): its flags, and the buffer its changes went to when it unloaded or was
    // demoted (StoreChangesOnUnload 35570), its live flags then cleared.
    struct ChangeFlags
    {
        uint32_t flags{};
        uint64_t storedBuffer{};
        uint32_t unkC{};
    };

    enum FormFlags
    {
        DELETED = 1 << 0x5,
        PERSISTENT = 1 << 0xA, // TESForm::IsPersistent (19593): a reference the engine keeps when its cell unloads
        DISABLED = 1 << 0xB,
        IGNORE_FRIENDLY_HITS = 1 << 0x14,
    };

    static TESForm* GetById(uint32_t aId);

    virtual void sub_4();
    virtual void sub_5();
    virtual void sub_6();
    virtual void sub_7();
    virtual void sub_8();
    virtual void sub_9();
    virtual bool MarkChanged(uint32_t aChangeFlag);
    virtual bool UnsetChanged(uint32_t aChangeFlag);
    virtual void sub_C();
    virtual void sub_D();
    virtual void Save(BGSSaveFormBuffer* apBuffer) const noexcept;
    virtual void Load(BGSLoadFormBuffer* apBuffer);
    virtual void InitLoadGame(BGSLoadFormBuffer* apBuffer);   // 0x10 (CommonLibSSE-NG)
    virtual void FinishLoadGame(BGSLoadFormBuffer* apBuffer); // 0x11
    virtual void sub_12();
    virtual void InitializeComponents(); // Called after Load
    virtual void sub_14();
    virtual FormType GetFormType();
    virtual void sub_16();
    virtual void sub_17();
    virtual void sub_18();
    virtual void sub_19();
    virtual void sub_1A();
    virtual void sub_1B();
    virtual void sub_1C();
    virtual void sub_1D();
    virtual void sub_1E();
    virtual void sub_1F();
    virtual void sub_20();
    virtual void sub_21();
    virtual void sub_22();
    // 14636 (TESObjectREFR's 19565): the deleted flag, recorded as form flags (AddChange 1); returns the old one.
    virtual bool SetDelete(bool aSet);
    virtual void sub_24();
    virtual void sub_25();
    virtual void sub_26();
    virtual void sub_27();
    virtual void sub_28();
    virtual void sub_29();
    virtual void sub_2A();
    virtual void sub_2B();
    virtual void sub_2C();
    virtual void sub_2D();
    // Actual function name is `GetTextForParsedSubTag`; passing an empty tag gets the full name of this form
    virtual const char* GetName(const BSFixedString& acTag = {}) const noexcept;
    virtual void CopyFrom(TESForm* apForm);
    virtual void sub_30();
    virtual void sub_31();
    virtual const char* GetFormEditorID();
    virtual void sub_33();
    virtual void sub_34();
    virtual void sub_35();
    virtual void sub_36();
    virtual void ActivateReference();
    virtual void sub_38();
    virtual void unk_39();
    virtual void unk_3A();

    // void CopyFromEx(TESForm* rhs);
    void SetSkipSaveFlag(bool aSet) noexcept;
    uint32_t GetChangeFlags() const noexcept;
    // Its changes sit in a stored buffer (StoreChangesOnUnload): kept, though no live flag shows them.
    bool HasStoredChanges() const noexcept;

    bool GetIgnoreFriendlyHit() const noexcept { return (flags & IGNORE_FRIENDLY_HITS) != 0; }
    void SetIgnoreFriendlyHit(bool aSet) noexcept
    {
        if (aSet)
            flags |= IGNORE_FRIENDLY_HITS;
        else
            flags &= ~IGNORE_FRIENDLY_HITS;
    }

    bool IsDisabled() const noexcept { return (flags & DISABLED) != 0; }
    bool IsDeleted() const noexcept { return (flags & DELETED) != 0; }
    void SetPersistent() noexcept { flags |= PERSISTENT; }
    bool IsTemporary() const noexcept { return formID >= 0xFF000000; }
    bool IsConsumable() const noexcept { return formType == FormType::Ingredient || formType == FormType::Alchemy; }

    uintptr_t unk4;
    uint32_t flags;           // 10
    uint32_t formID;          // 14
    uint16_t inGameFormFlags; // 18 (CommonLibSSE-NG InGameFormFlag)
    FormType formType;
    uint8_t padForm;
};

static_assert(sizeof(TESForm) == 0x20);
static_assert(offsetof(TESForm, flags) == 0x10);
static_assert(offsetof(TESForm, formID) == 0x14);
static_assert(offsetof(TESForm, inGameFormFlags) == 0x18);
static_assert(offsetof(TESForm, formType) == 0x1A);
