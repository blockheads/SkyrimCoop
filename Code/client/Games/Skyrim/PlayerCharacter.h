#pragma once

#include <Actor.h>
#include <Misc/TintMask.h>
#include <Forms/ActorValueInfo.h>

struct Skills
{
    enum Skill : std::uint32_t
    {
        kOneHanded = 0,
        kTwoHanded = 1,
        kArchery = 2,
        kBlock = 3,
        kSmithing = 4,
        kHeavyArmor = 5,
        kLightArmor = 6,
        kPickpocket = 7,
        kLockpicking = 8,
        kSneak = 9,
        kAlchemy = 10,
        kSpeech = 11,
        kAlteration = 12,
        kConjuration = 13,
        kDestruction = 14,
        kIllusion = 15,
        kRestoration = 16,
        kEnchanting = 17,
        kTotal
    };

    static const Skill GetSkillFromActorValue(int32_t aActorValue) noexcept
    {
        switch (aActorValue)
        {
        case ActorValueInfo::kOneHanded: return kOneHanded;
        case ActorValueInfo::kTwoHanded: return kTwoHanded;
        case ActorValueInfo::kMarksman: return kArchery;
        case ActorValueInfo::kBlock: return kBlock;
        case ActorValueInfo::kSmithing: return kSmithing;
        case ActorValueInfo::kHeavyArmor: return kHeavyArmor;
        case ActorValueInfo::kLightArmor: return kLightArmor;
        case ActorValueInfo::kPickpocket: return kPickpocket;
        case ActorValueInfo::kLockpicking: return kLockpicking;
        case ActorValueInfo::kSneak: return kSneak;
        case ActorValueInfo::kAlchemy: return kAlchemy;
        case ActorValueInfo::kSpeechcraft: return kSpeech;
        case ActorValueInfo::kAlteration: return kAlteration;
        case ActorValueInfo::kConjuration: return kConjuration;
        case ActorValueInfo::kDestruction: return kDestruction;
        case ActorValueInfo::kIllusion: return kIllusion;
        case ActorValueInfo::kRestoration: return kRestoration;
        case ActorValueInfo::kEnchanting: return kEnchanting;
        default: return kTotal;
        }
    }

    static const char* GetSkillString(Skill aSkill) noexcept
    {
        switch (aSkill)
        {
        case kOneHanded: return "One-handed";
        case kTwoHanded: return "Two-handed";
        case kArchery: return "Archery";
        case kBlock: return "Block";
        case kSmithing: return "Smithing";
        case kHeavyArmor: return "Heavy armor";
        case kLightArmor: return "Light armor";
        case kPickpocket: return "Pickpocket";
        case kLockpicking: return "Lockpicking";
        case kSneak: return "Sneak";
        case kAlchemy: return "Alchemy";
        case kSpeech: return "Speech";
        case kAlteration: return "Alteration";
        case kConjuration: return "Conjuration";
        case kDestruction: return "Destruction";
        case kIllusion: return "Illusion";
        case kRestoration: return "Restoration";
        case kEnchanting: return "Enchanting";
        default: return "UNKNOWN";
        }
    }

    struct SkillData
    {
        float level;
        float xp;
        float levelThreshold;
    };
    static_assert(sizeof(SkillData) == 0xC);

    float xp;
    float levelThreshold;
    SkillData skills[Skill::kTotal];
    uint32_t legendaryLevels[Skill::kTotal];
};

struct TESQuest;
struct TESTopicInfo;
struct BGSPerk;

struct PlayerCharacter : Actor
{
    static constexpr FormType Type = FormType::Character;
    static int32_t LastUsedCombatSkill;
    // The engine repositioned the player (coc) since the last call: its PositionPlayerEvent.
    // Experimental: only coc is validated; doors and fast travel may need a more generic hook.
    static bool ConsumeTeleported() noexcept;
    // Unregisters from the engine's event (before the DLL goes: a hot reload).
    static void StopTeleportEvents() noexcept;

    static PlayerCharacter* Get() noexcept;

    static void SetGodMode(bool aSet) noexcept;

    const GameArray<TintMask*>& GetTints() const noexcept;
    // baseTints again from its race's tint layers for its sex, at their default colours (40696, as the race menu).
    void RebuildTintMasks() noexcept;

    // TODO: there's an in game function for this in fallout 4, maybe also for skyrim?
    void SetDifficulty(const int32_t aDifficulty, bool aForceUpdate = true, bool aExpectGameDataLoaded = true) noexcept;

    void AddSkillExperience(int32_t aSkill, float aExperience) noexcept;
    // Whether the player heard this line within its reset hours (40694; TESTopic 25544 skips it then).
    [[nodiscard]] bool SaidRecently(const TESTopicInfo* apInfo) const noexcept;
    // Remembers the line as heard now, as saying it does (40692, from TESTopicInfo::Begin 25616).
    void RememberSaid(const TESTopicInfo* apInfo) noexcept;
    float GetSkillExperience(Skills::Skill aSkill) const noexcept { return (*pSkills)->skills[aSkill].xp; }

    NiPoint3 RespawnPlayer() noexcept;

    void PayCrimeGoldToAllFactions() noexcept;

    // The player's map marker (40535): apWorldOrCell is the worldspace or interior cell it is in, never null.
    void SetWaypoint(NiPoint3* apPosition, TESForm* apWorldOrCell) noexcept;
    void RemoveWaypoint() noexcept;

    struct Objective
    {
        BSFixedString name;
        TESQuest* quest;
    };

    struct ObjectiveInstance
    {
        Objective* instance;
        uint64_t instanceCount;
    };

    // A perk the player has, at its rank (CommonLibSSE-NG PerkRankData; saved by PlayerCharacter::SaveGame, vtable 0x0E).
    struct PerkRank
    {
        BGSPerk* perk;
        uint8_t rank;
    };

    uint8_t pad1[0x4B8 - sizeof(Actor)];
    GameArray<PerkRank*> perkRanks; // CommonLib addedPerks: what PlayerCharacter::AddPerk (40681) adds
    uint8_t pad4D0[0x588 - 0x4D0];
    GameArray<ObjectiveInstance> objectives;
    uint8_t pad5A0[0x8D0 - 0x5A0];
    uint32_t grabbedObject; // handle of what the player holds with Activate (DestroyMouseSprings 40557 clears it)
    uint8_t pad8D4[0x8EC - 0x8D4];
    uint32_t sleepSeconds; // a Sleep/Wait Menu rest's remaining game seconds (WaitStep 40485 counts it down)
    uint8_t pad8F0[0x92C - 0x8F0];
    uint32_t playerMapMarker; // handle of the marker placed on the map (40535 sets it, 40536 clears it)
    uint8_t pad930[0x938 - 0x930];
    uint32_t skillTrainingsThisLevel; // TrainingMenu::Train counts each training here
    uint8_t pad93C[0x9B8 - 0x93C];
    Skills** pSkills;
    uint8_t pad9B8[0xAC8 - 0x9B8];
    TESForm* locationForm;
    uint8_t padAC8[0x28];
    int32_t difficulty;
    uint32_t assumedIdentity;
    int8_t murder;
    uint8_t perkPoints; // CommonLib perkCount (Game.AddPerkPoints 55451)
    uint8_t padB0A[0xB18 - 0xB0A];
    GameArray<TintMask*> baseTints;
    GameArray<TintMask*>* overlayTints;
    uint8_t padB38[0xBE3 - 0xB38];
    // CommonLib PlayerFlags (0xBE0) byte 3; 0x20 isInCombat, set by UpdateInCombat (41238), read by IsInCombat (41024).
    uint8_t playerFlags3;
    uint8_t padBE4[0xBE8 - 0xBE4];

    [[nodiscard]] bool IsFlaggedInCombat() const noexcept { return playerFlags3 & 0x20; }
};

static_assert(offsetof(PlayerCharacter, perkRanks) == 0x4B8);
static_assert(offsetof(PlayerCharacter, objectives) == 0x588);
static_assert(offsetof(PlayerCharacter, difficulty) == 0xB00);
static_assert(offsetof(PlayerCharacter, perkPoints) == 0xB09);
static_assert(sizeof(PlayerCharacter::PerkRank) == 0x10);
static_assert(offsetof(PlayerCharacter, grabbedObject) == 0x8D0);
static_assert(offsetof(PlayerCharacter, sleepSeconds) == 0x8EC);
static_assert(offsetof(PlayerCharacter, playerMapMarker) == 0x92C);
static_assert(offsetof(PlayerCharacter, skillTrainingsThisLevel) == 0x938);
static_assert(offsetof(PlayerCharacter, pSkills) == 0x9B8);
static_assert(offsetof(PlayerCharacter, locationForm) == 0xAD0);
static_assert(offsetof(PlayerCharacter, baseTints) == 0xB18);
static_assert(offsetof(PlayerCharacter, overlayTints) == 0xB30);
static_assert(offsetof(PlayerCharacter, playerFlags3) == 0xBE3);
static_assert(sizeof(PlayerCharacter) == 0xBE8);
