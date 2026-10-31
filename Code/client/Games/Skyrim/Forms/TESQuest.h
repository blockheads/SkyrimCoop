#pragma once

#include <Games/Primitives.h>
#include <Misc/BSString.h>
#include <Components/TESFullName.h>
#include <Forms/BGSStoryManagerTree.h>
#include <Forms/BGSBaseAlias.h>
#include <TESObjectREFR.h>

struct BGSLocation;

struct BGSScene : TESForm
{
    GameArray<void*> phases;
    GameArray<uint32_t> actorIds;
};

struct TESQuest : BGSStoryManagerTreeForm
{
    enum class State : uint8_t
    {
        WaitingPromotion,
        Running,
        Stopped,
        WaitingForStage
    };

    enum Flags : uint16_t
    {
        StopStart = USHRT_MAX,
        None = 0,
        Enabled = 1 << 0,
        Completed = 1 << 1,
        AddIdleToHello = 1 << 2,
        AllowRepeatStages = 1 << 3,
        StartsEnabled = 1 << 4,
        DisplayedInHUD = 1 << 5,
        Failed = 1 << 6,
        StageWait = 1 << 7,
        RunOnce = 1 << 8,
        ExcludeFromExport = 1 << 9,
        WarnOnAliasFillFailure = 1 << 10,
        Active = 1 << 11,
        RepeatsConditions = 1 << 12,
        KeepInstance = 1 << 13,
        WantDormant = 1 << 14,
        HasDialogueData = 1 << 15
    };

    enum class Type : uint8_t
    {
        None = 0,
        MainQuest = 1,
        MagesGuild = 2,
        ThievesGuild = 3,
        DarkBrotherhood = 4,
        CompanionsQuest = 5,
        Miscellaneous = 6,
        Daedric = 7,
        SideQuest = 8,
        CivilWar = 9,
        DLC01_Vampire = 10,
        DLC02_Dragonborn = 11
    };

    // BGSQuestObjective (CommonLibSSE-NG).
    struct Objective
    {
        BSFixedString nameRef; // 0x0000
        TESQuest* parent;      // 0x0008
        char pad10[12];        // 0x0010
        uint16_t index;        // 0x001C
        uint8_t pad1E;
        uint8_t state; // 0x001F: 0 hidden, 1 displayed, 2/3 completed, 4/5 failed

        // As Papyrus' SetObjectiveDisplayed/Completed/Failed (56681-56683) set it (23933): the change, the HUD's messages.
        void SetState(uint8_t aState) noexcept;
    };

    static_assert(offsetof(Objective, parent) == 0x8);
    static_assert(offsetof(Objective, index) == 0x1C);
    static_assert(offsetof(Objective, state) == 0x1F);

    struct Stage
    {
        uint16_t stageIndex;
        uint8_t flags; // 1: done (TESQuest::LoadGame 25030 sets only this)

        bool IsDone() const noexcept { return flags & 1; }
        void SetDone(bool aDone) noexcept { flags = aDone ? (flags | 1) : (flags & ~1); }
    };

    // TESForm change flags of a quest (CommonLibSSE-NG TESQuest::ChangeFlags).
    enum ChangeFlags : uint32_t
    {
        kChangeQuestFlags = 1 << 1,
        kChangeObjectives = 1 << 29,
        kChangeStages = 1u << 31,
    };

    TESFullName fullName;
    GameArray<void*> instanceData;
    uint32_t currentInstanceID;
    GameArray<BGSBaseAlias*> aliases; // 0x0058
    char pad70[0xD8 - 0x70];
    float questDelay;     // 0x00D8
    uint16_t flags;       // 0x00DC default init: 256
    uint8_t priority;     // 0x00DE
    Type type;            // 0x00DF
    int32_t scopedStatus; // 0x00E0 default init: -1, if not -1 outside of story manager scope
    uint32_t padE4;
    GameList<Stage> stages;
    /*
    GameList<Stage>* pExecutedStages;  // 0x00E8
    GameList<Stage>* pWaitingStages;   // 0x00F0
    */
    GameList<Objective> objectives; // 0x00F8
    char pad108[0x100];             // 0x0108
    GameArray<BGSScene*> scenes;    // 0x0208
    char pad210[8];                 // 0x0210
    uint16_t currentStage;          // 0x0228
    bool alreadyRun;                // 0x022A
    char pad22B[2];
    BSString idName; // < this is the proper quest ID
    void* pStartEventData;
    uint64_t unkFlags;
    char pad250[24];

    TESObjectREFR* GetAliasedRef(uint32_t aiAliasID) noexcept;
    // The reference alias with that id (25042); none for a location alias.
    BGSBaseAlias* GetRefAlias(uint32_t aAliasId) noexcept;
    // ReferenceAlias.ForceRefTo (25052, CommonLibSSE-NG ForceRefIntoAlias): fills it, gives the reference the alias's data.
    void ForceRefIntoAlias(uint32_t aAliasId, TESObjectREFR* apRef) noexcept;
    // ReferenceAlias.Clear (25051).
    void ClearRefAlias(BGSBaseAlias* apAlias) noexcept;
    // The alias with that id, any kind.
    BGSBaseAlias* GetAlias(uint32_t aAliasId) noexcept;
    // A location alias's fill (LocationAlias.GetLocation: 25067, the quest's location fills under its alias lock).
    BGSLocation* GetAliasedLocation(uint32_t aAliasId) noexcept;
    // LocationAlias.ForceLocationTo (25054): clears the old fill, fills it; a reference alias is left.
    void ForceLocationIntoAlias(uint32_t aAliasId, BGSLocation* apLocation) noexcept;
    // LocationAlias.Clear (25053).
    void ClearLocationAlias(BGSBaseAlias* apAlias) noexcept;

    Stage* GetStage(uint16_t aIndex) noexcept;
    Objective* GetObjective(uint16_t aIndex) noexcept;

    bool IsStageDone(uint16_t stageIndex);
    // Sets or clears Completed (24991): the change, the QuestStatus event, the HUD's message.
    void SetCompleted(bool aCompleted);
    // Sets or clears Failed (24988): the change, the HUD's message.
    void SetFailed(bool aFailed) noexcept;
    // Its topics' lines read from the plugins, as its start does (25086, from QueuedPromoteQuestTask): a quest not
    // started with the game has no lines (TESTopicInfo forms) until then. A topic's loaded ones are kept.
    void LoadDialogue() noexcept;

    void CompleteAllObjectives(); // completes all objectives + stages
    void SetActive(bool toggle);  // < is the quest selected in journal and followed?

    inline void Disable() { flags &= ~Flags::Enabled; };

    inline bool IsEnabled() const { return flags & Flags::Enabled; }
    inline bool IsActive() const { return flags & Flags::Active; }
    inline bool IsStopped() const { return (flags & (Flags::Enabled | Flags::StageWait)) == 0; } // & 0x81
    // Running, not waiting on a stage: as TESTopic's line lookup (25544) asks of a topic's quest.
    inline bool IsRunning() const { return (flags & Flags::Enabled) && !(flags & Flags::StageWait); }

    bool Kill();
    State getState();

    bool EnsureQuestStarted(bool& succeded, bool force);

    bool SetStage(uint16_t stage);
    // SetStage with each stage item's fragment skipped, as the VM skips an item without one (a guest
    // applying the host's stage: the host's game ran it). docs/re/systems/quest-apply.md.
    bool SetStageWithoutFragments(uint16_t aStage) noexcept;
    void ScriptSetStage(uint16_t stage);
    void SetStopped();
};

static_assert(sizeof(TESQuest) == 0x268);
static_assert(offsetof(TESQuest, fullName) == 0x28);
static_assert(offsetof(TESQuest, aliases) == 0x58);
static_assert(offsetof(TESQuest, flags) == 0xDC);
static_assert(offsetof(TESQuest, stages) == 0xE8);
static_assert(offsetof(TESQuest, objectives) == 0xF8);
static_assert(offsetof(TESQuest, currentStage) == 0x228);
static_assert(offsetof(TESQuest, unkFlags) == 0x248);
