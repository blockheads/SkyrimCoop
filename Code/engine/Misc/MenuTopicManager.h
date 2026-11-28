#pragma once

#include <Misc/BSString.h>

#include <vector>
#include <Primitives.h>

struct TESForm;
struct TESQuest;
struct TESTopic;
struct TESTopicInfo;

// The player's conversation (CommonLibSSE-NG MenuTopicManager). docs/re/systems/dialogue.md.
struct MenuTopicManager
{
    // One option in the dialogue menu.
    struct Dialogue
    {
        BSString topicText;             // 00, the line shown
        bool hasChoices;                // 10, its info has choices: the list build (35287) lists them, not the top level
        uint8_t unk11[0x7];             // 11
        GameList<void> responses;       // 18, DialogueResponse
        TESQuest* parentQuest;          // 28
        TESTopicInfo* parentTopicInfo;  // 30, the info said when picked
        TESForm* parentTopic;           // 38, a TESTopic
        GameList<void>::Entry* currentResponse; // 40, the response said next (SaySelected 35283 says nothing without one)
        bool notChoices;                // 48, set: the build (35287) does not take its choices
        bool neverSaid;                 // 49
        bool hidden;                    // 4A, not listed by the menu's fill (51515), still counted
        uint8_t unk4B[0x58 - 0x4B];     // 4B
    };

    static MenuTopicManager* Get() noexcept;

    // The options in menu order, as SelectDialogue (35269) counts them. Valid until the engine rebuilds the list:
    // use them on the main thread (where the engine builds and picks), within the frame.
    [[nodiscard]] std::vector<Dialogue*> Options() noexcept;
    // Whether there is an option to show (35267, as the menu asks before filling its list), the picked node unchanged.
    [[nodiscard]] bool HasOptions() noexcept;

    // Selects and says the option at this index, as the menu's TopicClicked does (51509: 35269, then 35283).
    void Pick(uint32_t aIndex) noexcept;
    // Moves past the line being said, as the menu's SkipText does (35290).
    void SkipLine() noexcept;
    // Clears the conversation as starting and ending one do (35274): no speaker, no list, every flag down.
    void Clear() noexcept;
    // The topics of the said line's choices (rootTopicInfo's, 25659 as the list build 35287 reads them), in order,
    // whether this game's player gets a line for them or not; none when the list is not a line's choices.
    [[nodiscard]] std::vector<TESTopic*> Choices() noexcept;

    // Changing the list. BuildOption makes an option for the topic said with this info, as AddTopic (35303) does
    // once the info's conditions pass (Dialogue's constructor 35254, with the speaker); no conditions are tested,
    // and it is not in the list until added.
    [[nodiscard]] Dialogue* BuildOption(TESTopic* apTopic, TESTopicInfo* apInfo) noexcept;
    // The list becomes these options, in order (its own or built here); the ones left out are destroyed.
    void SetOptions(const std::vector<Dialogue*>& acOptions) noexcept;
    // Dialogue's deleting destructor (35319).
    static void Destroy(Dialogue* apOption) noexcept;

    void* sinkMenuOpenClose;                         // 00, BSTEventSink<MenuOpenCloseEvent>
    void* sinkPositionPlayer;                        // 08, BSTEventSink<PositionPlayerEvent>
    uint8_t singleton[0x8];                          // 10, BSTSingletonSDM
    GameList<Dialogue>::Entry* selectedResponseNode; // 18, set by SelectDialogue (35269)
    GameList<Dialogue>* dialogueList;                // 20, the options in menu order
    void* dialogueListStack;                         // 28
    TESTopicInfo* rootTopicInfo;                     // 30, the said info whose choices the list is (35287), else null
    Dialogue* lastSelectedDialogue;                  // 38
    CRITICAL_SECTION criticalSection;                // 40, held around a pick
    uint32_t speaker;                                // 68, handle
    uint32_t lastSpeaker;                            // 6C, handle: a speaker still talking when its conversation ended (35294)
    TESTopicInfo* currentTopicInfo;                  // 70
    TESTopicInfo* lastTopicInfo;                     // 78, that speaker's line, ended by the next start with another (35282)
    GameArray<TESForm*> blockingBranches;            // 80, BGSDialogueBranch
    GameArray<TESForm*> topLevelBranches;            // 98
    bool isGreetingPlayer;                           // B0
    bool menuOpen;                                   // B1
    bool forceGoodbye;                               // B2
    bool shutMenu;                                   // B3
    bool rumorTopicAdded;                            // B4
    bool waitingToAdvance;                           // B5
    bool advanceRequested;                           // B6
    bool skipHeadResponse;                           // B7, the list's first node is not an option
    bool advanceInProgress;                          // B8
    bool lineSkipped;                                // B9
    bool canSkip;                                    // BA
    bool closeInProgress;                            // BB
    uint32_t padBC;                                  // BC
    GameArray<TESForm*> topicHistory;                // C0, TESTopic

private:
    using Entry = GameList<Dialogue>::Entry;

    // The first node holding an option: the head, or the one after it when the head is the greeting's.
    [[nodiscard]] Entry* FirstOptionNode() noexcept;
    // SetOptions' work, under the lock with a list: the options no longer wanted, or left without a node, to aDropped.
    void Relink(const std::vector<Dialogue*>& acOptions, std::vector<Dialogue*>& aDropped) noexcept;
    // A list node holding the option (null: out of memory).
    [[nodiscard]] static Entry* NewNode(Dialogue* apOption) noexcept;
};

static_assert(sizeof(MenuTopicManager::Dialogue) == 0x58);
static_assert(offsetof(MenuTopicManager::Dialogue, hasChoices) == 0x10);
static_assert(offsetof(MenuTopicManager::Dialogue, responses) == 0x18);
static_assert(offsetof(MenuTopicManager::Dialogue, notChoices) == 0x48);
static_assert(offsetof(MenuTopicManager::Dialogue, parentTopicInfo) == 0x30);
static_assert(offsetof(MenuTopicManager::Dialogue, currentResponse) == 0x40);
static_assert(offsetof(MenuTopicManager, dialogueList) == 0x20);
static_assert(offsetof(MenuTopicManager, criticalSection) == 0x40);
static_assert(offsetof(MenuTopicManager, speaker) == 0x68);
static_assert(offsetof(MenuTopicManager, lastSpeaker) == 0x6C);
static_assert(offsetof(MenuTopicManager, lastTopicInfo) == 0x78);
static_assert(offsetof(MenuTopicManager, selectedResponseNode) == 0x18);
static_assert(offsetof(MenuTopicManager, lastSelectedDialogue) == 0x38);
static_assert(offsetof(MenuTopicManager, blockingBranches) == 0x80);
static_assert(offsetof(MenuTopicManager, menuOpen) == 0xB1);
static_assert(offsetof(MenuTopicManager, forceGoodbye) == 0xB2);
static_assert(offsetof(MenuTopicManager, shutMenu) == 0xB3);
static_assert(offsetof(MenuTopicManager, closeInProgress) == 0xBB);
static_assert(offsetof(MenuTopicManager, skipHeadResponse) == 0xB7);
static_assert(offsetof(MenuTopicManager, topicHistory) == 0xC0);
static_assert(sizeof(MenuTopicManager) == 0xD8);
