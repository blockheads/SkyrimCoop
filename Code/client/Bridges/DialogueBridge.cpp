#include <Bridges/DialogueBridge.h>

#include <Actor.h>
#include <Forms/ActorValueInfo.h>
#include <Forms/TESForm.h>
#include <Forms/TESQuest.h>
#include <Forms/TESTopic.h>
#include <Forms/TESTopicInfo.h>
#include <Games/ActorExtension.h>
#include <Games/Misc/MenuTopicManager.h>
#include <HookSwitch.h>
#include <Interface/FxDelegateArgs.h>
#include <Interface/MenuOpenCloseEvent.h>
#include <Interface/UI.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>

namespace
{
// An option in the engine's list (IDialogueBridge::Option is what the service sees of one).
using EngineOption = MenuTopicManager::Dialogue;

constexpr const char* kDialogueMenu = "Dialogue Menu";

// ---- State ----

IDialogueBridge::Filters s_filters;
// Starting a conversation without its menu, and one under way without it (docs/re/systems/dialogue.md).
bool s_startingHidden = false;
bool s_hidden = false;
// The menu closing leaves the conversation on (MenuTopicManager's close sink skipped).
bool s_keepConversation = false;
// The lists the engine has shown (ShowList 52857).
uint32_t s_listsShown = 0;

// Options built here and put in the engine's list. Every list the engine shows it has just built (35287 before each
// of its 52857 calls), freeing the last one with ours: the record starts over there (HookShowList).
std::vector<const EngineOption*> s_added;

// ---- Helpers ----

uint32_t IdOf(const TESForm* apForm) noexcept
{
    return apForm ? apForm->formID : 0;
}

template <class T> T* FormAs(uint32_t aFormId) noexcept
{
    return Cast<T>(TESForm::GetById(aFormId));
}

uint32_t TopicOf(const EngineOption& acOption) noexcept
{
    return IdOf(acOption.parentTopic);
}

uint32_t InfoOf(const EngineOption& acOption) noexcept
{
    return IdOf(acOption.parentTopicInfo);
}

// The conversation under way: the manager while it has a speaker, else null.
MenuTopicManager* Conversation() noexcept
{
    MenuTopicManager* pManager = MenuTopicManager::Get();
    return pManager && TESObjectREFR::GetByHandle(pManager->speaker) ? pManager : nullptr;
}

bool IsMenuShown() noexcept
{
    const UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(kDialogueMenu);
}

// Closes the open menu as the engine does: a kHide whose data is set plays the SWF's fade-out first
// (DialogueMenu::ProcessMessage 51506), whose CloseMenu callback (51511) gives the HUD its subtitles back.
bool HideMenu() noexcept
{
    UIMessageQueue* pQueue = UIMessageQueue::Get();
    BSUIMessageData* pData = pQueue && IsMenuShown() ? UIMessageQueue::CreateData() : nullptr;
    if (!pData)
        return false;
    pData->data.b = true;
    pQueue->AddMessage(kDialogueMenu, UIMessage::kHide, pData);
    return true;
}

bool IsAdded(const EngineOption* apOption) noexcept
{
    return std::ranges::find(s_added, apOption) != s_added.end();
}

// The option among these saying the topic with this line (null: none).
EngineOption* FindLine(const std::vector<EngineOption*>& acOptions, uint32_t aTopic, uint32_t aInfo) noexcept
{
    const auto cIt = std::ranges::find_if(acOptions, [aTopic, aInfo](const EngineOption* apOption) {
        return TopicOf(*apOption) == aTopic && InfoOf(*apOption) == aInfo;
    });
    return cIt != acOptions.end() ? *cIt : nullptr;
}

// An option for the topic said with this line, built and recorded as added (null: nothing to say).
EngineOption* Build(MenuTopicManager& aManager, TESTopic* apTopic, TESTopicInfo* apInfo) noexcept
{
    EngineOption* pOption = aManager.BuildOption(apTopic, apInfo);
    if (pOption)
        s_added.push_back(pOption);
    return pOption;
}

// The list becomes these options; the added ones left out are destroyed with it.
void SetList(MenuTopicManager& aManager, const std::vector<EngineOption*>& acOptions) noexcept
{
    aManager.SetOptions(acOptions);
    std::erase_if(s_added, [&acOptions](const EngineOption* apAdded) {
        return std::ranges::find(acOptions, apAdded) == acOptions.end();
    });
}

// Whether this game can say the line for the topic: one of the topic's, not a say-once line said here, not one its
// player heard within the line's reset hours.
bool CanSay(const TESTopic* apTopic, const TESTopicInfo* apInfo) noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return apInfo && apInfo->CanSayFor(apTopic) && pPlayer && !pPlayer->SaidRecently(apInfo);
}

// A question this game's player has no answer to (a line's choices, none listed), which the engine would end the
// conversation on: when the service asks for it, its choices stay in the list unlisted, each with its topic's first
// line, for another player's answer to take its place. True: the menu closes on it, the list is not shown.
bool HoldChoices(MenuTopicManager& aManager) noexcept
{
    if (!aManager.rootTopicInfo || aManager.HasOptions() || !s_filters.HoldChoices || !s_filters.HoldChoices())
        return false;
    for (TESTopic* pTopic : aManager.Choices())
        if (EngineOption* pOption = pTopic->numTopicInfos ? Build(aManager, pTopic, pTopic->topicInfos[0]) : nullptr)
        {
            pOption->hidden = true;
            aManager.Append(pOption);
        }
    return HideMenu();
}

// A conversation without its menu ends where the menu's fill (51515) would end it: a goodbye said, or nothing left to
// say. Held choices count as something to say (HasOptions counts unlisted options).
void EndWhenDone(MenuTopicManager& aManager) noexcept
{
    if (aManager.forceGoodbye || !aManager.HasOptions())
        aManager.shutMenu = true;
}

// ---- Hooks ----

// Actor::SetDialogueWithPlayer (37200, CommonLibSSE-NG vfunc 0x41): starts (aStart) or stops the player's
// conversation with the actor; activating an NPC and force greets come here.
TP_THIS_FUNCTION(TSetDialogueWithPlayer, bool, Actor, bool aStart, bool aForceGreet, TESTopicInfo* apTopic);
TSetDialogueWithPlayer* RealSetDialogueWithPlayer = nullptr;

bool TP_MAKE_THISCALL(HookSetDialogueWithPlayer, Actor, bool aStart, bool aForceGreet, TESTopicInfo* apTopic)
{
    if (aStart && s_filters.Start && !s_filters.Start(apThis->formID, aForceGreet))
        return false;
    return TiltedPhoques::ThisCall(RealSetDialogueWithPlayer, apThis, aStart, aForceGreet, apTopic);
}

// TESTopicInfo::Begin (25616): a line starts, its speaker's: runs its fragment (the topic info event) and
// marks it said. A copy's lines are its owner's: its owner's game runs them.
TP_THIS_FUNCTION(TBeginTopicInfo, uint64_t, TESTopicInfo, int32_t aStage, uint32_t* apSpeakerHandle, bool aAnySpeaker);
TBeginTopicInfo* RealBeginTopicInfo = nullptr;

uint64_t TP_MAKE_THISCALL(HookBeginTopicInfo, TESTopicInfo, int32_t aStage, uint32_t* apSpeakerHandle, bool aAnySpeaker)
{
    Actor* pSpeaker = apSpeakerHandle ? Cast<Actor>(TESObjectREFR::GetByHandle(*apSpeakerHandle)) : nullptr;
    if (pSpeaker && pSpeaker->GetExtension()->IsRemote())
        return 0;
    return TiltedPhoques::ThisCall(RealBeginTopicInfo, apThis, aStage, apSpeakerHandle, aAnySpeaker);
}

// TESNPC::Activate (24715, CommonLibSSE-NG TESBoundObject::Activate): the player talking to an NPC. Talking to
// its last speaker again can do nothing (a copy never leaves its conversation), so joins are decided here.
TP_THIS_FUNCTION(TNpcActivate, bool, TESForm, TESObjectREFR* apTarget, TESObjectREFR* apActivator, uint8_t aUnk1,
                 TESForm* apObject, int32_t aCount);
TNpcActivate* RealNpcActivate = nullptr;

bool TP_MAKE_THISCALL(HookNpcActivate, TESForm, TESObjectREFR* apTarget, TESObjectREFR* apActivator, uint8_t aUnk1,
                      TESForm* apObject, int32_t aCount)
{
    if (s_filters.Talk && apTarget && apActivator == PlayerCharacter::Get() && s_filters.Talk(apTarget->formID))
        return true;
    return TiltedPhoques::ThisCall(RealNpcActivate, apThis, apTarget, apActivator, aUnk1, apObject, aCount);
}

// DialogueMenu's TopicClicked (51509): the SWF's click on an option, its index in the list; the original selects
// and says it (35269, 35283).
using TTopicClicked = void(const FxDelegateArgs* apArgs);
TTopicClicked* RealTopicClicked = nullptr;

void HookTopicClicked(const FxDelegateArgs* apArgs)
{
    const bool cIndexGiven = apArgs && apArgs->menu && apArgs->numArgs == 1 &&
                             (apArgs->args[0].type & GFxValue::kTypeMask) == GFxValue::kNumber;
    if (cIndexGiven && s_filters.Pick && s_filters.Pick(static_cast<uint32_t>(apArgs->args[0].value.number)))
        return;
    RealTopicClicked(apArgs);
}

// Shows the dialogue menu with the speaker's name (52853, from MenuTopicManager::StartDialogue 35282).
using TShowMenu = uint64_t(const char* acpSpeakerName);
TShowMenu* RealShowMenu = nullptr;

uint64_t HookShowMenu(const char* acpSpeakerName)
{
    if (s_startingHidden)
        return 0;
    s_hidden = false;
    return RealShowMenu(acpSpeakerName);
}

// Turns the player to the speaker (40496, PlayerCharacter; called only from 35282).
TP_THIS_FUNCTION(TTurnToSpeaker, void, PlayerCharacter);
TTurnToSpeaker* RealTurnToSpeaker = nullptr;

void TP_MAKE_THISCALL(HookTurnToSpeaker, PlayerCharacter)
{
    if (!s_startingHidden)
        TiltedPhoques::ThisCall(RealTurnToSpeaker, apThis);
}

// Asks the menu to show the list (52857), which the engine has just built (35287): after each line, and at the
// start. The menu's fill then ends the conversation when nothing is left to say or a goodbye was said (51515).
using TShowList = void(bool aNewList);
TShowList* RealShowList = nullptr;

void HookShowList(bool aNewList)
{
    ++s_listsShown;
    s_added.clear();
    if (s_filters.List && s_filters.List())
        return;
    MenuTopicManager* pManager = MenuTopicManager::Get();
    if (pManager && HoldChoices(*pManager))
        return;
    RealShowList(aNewList);
    if (pManager && s_hidden)
        EndWhenDone(*pManager);
}

// MenuTopicManager's menu close sink (35272): the dialogue menu closing ends the conversation (shutMenu).
TP_THIS_FUNCTION(TMenuOpenClose, uint32_t, MenuTopicManager, const MenuOpenCloseEvent* apEvent, void* apSource);
TMenuOpenClose* RealMenuOpenClose = nullptr;

uint32_t TP_MAKE_THISCALL(HookMenuOpenClose, MenuTopicManager, const MenuOpenCloseEvent* apEvent, void* apSource)
{
    const char* cpMenu = apEvent ? apEvent->menuName.AsAscii() : nullptr;
    const bool cDialogueClosing = cpMenu && !apEvent->opening && std::strcmp(cpMenu, kDialogueMenu) == 0;
    if (s_keepConversation && cDialogueClosing)
    {
        spdlog::debug("Dialogue: the menu closes, the conversation stays on");
        s_hidden = true;
        apThis->closeInProgress = false;
        return 0;
    }
    return TiltedPhoques::ThisCall(RealMenuOpenClose, apThis, apEvent, apSource);
}

// Opens an actor's training menu (52942, Game.ShowTrainingMenu 55582's only call): queues the Training Menu's show
// with the trainer.
using TShowTraining = void(Actor* apTrainer);
TShowTraining* RealShowTraining = nullptr;

void HookShowTraining(Actor* apTrainer)
{
    if (s_filters.Training && apTrainer && s_filters.Training(apTrainer->formID))
        return;
    RealShowTraining(apTrainer);
}

TiltedPhoques::Initializer s_dialogueHooks([] {
    const HookGate cGate(__FILE__);
    POINTER_SKYRIMSE(TSetDialogueWithPlayer, s_setDialogueWithPlayer, 37200);
    POINTER_SKYRIMSE(TBeginTopicInfo, s_beginTopicInfo, 25616);
    POINTER_SKYRIMSE(TNpcActivate, s_npcActivate, 24715);
    POINTER_SKYRIMSE(TTopicClicked, s_topicClicked, 51509);
    POINTER_SKYRIMSE(TShowMenu, s_showMenu, 52853);
    POINTER_SKYRIMSE(TTurnToSpeaker, s_turnToSpeaker, 40496);
    POINTER_SKYRIMSE(TShowList, s_showList, 52857);
    POINTER_SKYRIMSE(TMenuOpenClose, s_menuOpenClose, 35272);
    POINTER_SKYRIMSE(TShowTraining, s_showTraining, 52942);

    RealSetDialogueWithPlayer = s_setDialogueWithPlayer.Get();
    RealBeginTopicInfo = s_beginTopicInfo.Get();
    RealNpcActivate = s_npcActivate.Get();
    RealTopicClicked = s_topicClicked.Get();
    RealShowMenu = s_showMenu.Get();
    RealTurnToSpeaker = s_turnToSpeaker.Get();
    RealShowList = s_showList.Get();
    RealMenuOpenClose = s_menuOpenClose.Get();
    RealShowTraining = s_showTraining.Get();

    TP_HOOK(&RealSetDialogueWithPlayer, HookSetDialogueWithPlayer);
    TP_HOOK(&RealBeginTopicInfo, HookBeginTopicInfo);
    TP_HOOK(&RealNpcActivate, HookNpcActivate);
    TP_HOOK(&RealTopicClicked, HookTopicClicked);
    TP_HOOK(&RealShowMenu, HookShowMenu);
    TP_HOOK(&RealTurnToSpeaker, HookTurnToSpeaker);
    TP_HOOK(&RealShowList, HookShowList);
    TP_HOOK(&RealMenuOpenClose, HookMenuOpenClose);
    TP_HOOK(&RealShowTraining, HookShowTraining);
});
} // namespace

DialogueBridge::DialogueBridge() noexcept = default;

DialogueBridge::~DialogueBridge()
{
    SetFilters({});
}

void DialogueBridge::SetFilters(Filters aFilters) noexcept
{
    s_filters = std::move(aFilters);
    PlayerCharacter::SetSpeechExperienceFilter(s_filters.SpeechExperience);
}

// ---- Reading the conversation ----

uint32_t DialogueBridge::GetSpeaker() const noexcept
{
    const MenuTopicManager* pManager = Conversation();
    return pManager ? IdOf(TESObjectREFR::GetByHandle(pManager->speaker)) : 0;
}

bool DialogueBridge::IsMenuOpen() const noexcept
{
    return IsMenuShown();
}

std::vector<IDialogueBridge::Option> DialogueBridge::GetOptions() const noexcept
{
    std::vector<Option> options;
    MenuTopicManager* pManager = MenuTopicManager::Get();
    if (!pManager)
        return options;
    for (const EngineOption* pListed : pManager->Options())
    {
        const char* cpText = pListed->topicText.AsAscii();
        options.push_back(
            {TopicOf(*pListed), InfoOf(*pListed), IdOf(pListed->parentQuest), cpText ? cpText : "", IsAdded(pListed)});
    }
    return options;
}

bool DialogueBridge::ShowsChoices() const noexcept
{
    const MenuTopicManager* pManager = MenuTopicManager::Get();
    return pManager && pManager->rootTopicInfo;
}

std::vector<IDialogueBridge::Option> DialogueBridge::GetChoices() const noexcept
{
    std::vector<Option> choices;
    if (MenuTopicManager* pManager = MenuTopicManager::Get())
        for (const TESTopic* pTopic : pManager->Choices())
            choices.push_back({.Topic = pTopic->formID, .Quest = IdOf(pTopic->ownerQuest)});
    return choices;
}

uint32_t DialogueBridge::GetListsShown() const noexcept
{
    return s_listsShown;
}

std::optional<uint32_t> DialogueBridge::GetLine(uint32_t aTopic) const noexcept
{
    // TESTopic's line for a speaker and the player (25544, as AddTopic 35303 calls it): its quest running, its
    // first line whose conditions pass (or a random one of them), none said too recently.
    using TGetLine = TESTopicInfo*(TESTopic*, bool* apRandomEnd, TESObjectREFR* apSpeaker, TESObjectREFR* apPlayer,
                                   bool aUnk, void* apUnk, void* apHistory);
    POINTER_SKYRIMSE(TGetLine, s_getLine, 25544);
    const MenuTopicManager* pManager = Conversation();
    auto* pTopic = FormAs<TESTopic>(aTopic);
    if (!pManager || !pTopic || !pTopic->ownerQuest || !pTopic->ownerQuest->IsRunning())
        return std::nullopt;
    bool randomEnd = false;
    const TESTopicInfo* pInfo = s_getLine.Get()(pTopic, &randomEnd, TESObjectREFR::GetByHandle(pManager->speaker),
                                                PlayerCharacter::Get(), false, nullptr, nullptr);
    return IdOf(pInfo);
}

// ---- Starting and ending it ----

bool DialogueBridge::Start(uint32_t aActorFormId, bool aShowMenu) noexcept
{
    // Nobody talks while fighting or dead, as talking to them does; a start from code would run all the same.
    Actor* pActor = FormAs<Actor>(aActorFormId);
    if (!pActor || pActor->IsDead() || pActor->IsInCombat())
        return false;
    s_startingHidden = !aShowMenu;
    const bool cStarted = TiltedPhoques::ThisCall(RealSetDialogueWithPlayer, pActor, true, false, nullptr);
    s_startingHidden = false;
    s_hidden = cStarted && !aShowMenu;
    return cStarted;
}

bool DialogueBridge::ShowMenu() noexcept
{
    // TESObjectREFR::GetDisplayFullName (19781): the name StartDialogue gives the menu.
    using TGetDisplayFullName = const char*(TESObjectREFR*);
    POINTER_SKYRIMSE(TGetDisplayFullName, s_getDisplayFullName, 19781);
    MenuTopicManager* pManager = Conversation();
    if (!pManager || IsMenuShown())
        return false;
    s_hidden = false;
    RealShowMenu(s_getDisplayFullName.Get()(TESObjectREFR::GetByHandle(pManager->speaker)));
    RealShowList(true);
    TiltedPhoques::ThisCall(RealTurnToSpeaker, PlayerCharacter::Get());
    return true;
}

void DialogueBridge::CloseMenu() noexcept
{
    // The menu closing ends the conversation (35272), unless SetMenuCloseEnds(false).
    HideMenu();
}

void DialogueBridge::SetMenuCloseEnds(bool aEnds) noexcept
{
    s_keepConversation = !aEnds;
}

void DialogueBridge::End() noexcept
{
    if (MenuTopicManager* pManager = MenuTopicManager::Get())
        pManager->shutMenu = true;
}

bool DialogueBridge::ClearOrphaned() noexcept
{
    MenuTopicManager* pManager = MenuTopicManager::Get();
    if (!pManager || !pManager->menuOpen || TESObjectREFR::GetByHandle(pManager->speaker))
        return false;
    pManager->Clear();
    s_added.clear();
    return true;
}

// ---- The list and picks ----

bool DialogueBridge::ShowOptions(const std::vector<Option>& acOptions) noexcept
{
    MenuTopicManager* pManager = Conversation();
    if (!pManager)
        return false;
    // Each line once (one option in two of the list's nodes would be freed twice): from the list when it has it,
    // else built.
    const std::vector<EngineOption*> cListed = pManager->Options();
    std::vector<EngineOption*> wanted;
    for (const Option& cOption : acOptions)
    {
        if (FindLine(wanted, cOption.Topic, cOption.Info))
            continue;
        EngineOption* pOption = FindLine(cListed, cOption.Topic, cOption.Info);
        if (!pOption)
            pOption = Build(*pManager, FormAs<TESTopic>(cOption.Topic), FormAs<TESTopicInfo>(cOption.Info));
        if (pOption)
            wanted.push_back(pOption);
    }
    // Nothing built and the same order: nothing changed.
    if (wanted == cListed)
        return false;
    SetList(*pManager, wanted);
    Redraw();
    return true;
}

void DialogueBridge::Redraw() noexcept
{
    // The menu fills its SWF from the list again (51515), as it does for a new list; voice ready (52858): the SWF
    // takes a pick 750 ms later, as after a line.
    using TVoiceReady = void();
    POINTER_SKYRIMSE(TVoiceReady, s_voiceReady, 52858);
    if (!IsMenuShown())
        return;
    RealShowList(false);
    s_voiceReady.Get()();
}

void DialogueBridge::Pick(uint32_t aIndex) noexcept
{
    if (MenuTopicManager* pManager = MenuTopicManager::Get())
        pManager->Pick(aIndex);
}

bool DialogueBridge::PickTopic(uint32_t aTopic, uint32_t aInfo) noexcept
{
    MenuTopicManager* pManager = Conversation();
    auto* pTopic = FormAs<TESTopic>(aTopic);
    auto* pInfo = FormAs<TESTopicInfo>(aInfo);
    if (!pManager || !pTopic)
        return false;

    // The option to say: the list's with that line; else that line built, in the place of the topic's option (or
    // last); else, a line this game cannot say, the topic's own option.
    std::vector<EngineOption*> options = pManager->Options();
    const auto cTopicOption =
        std::ranges::find_if(options, [aTopic](const EngineOption* apOption) { return TopicOf(*apOption) == aTopic; });
    EngineOption* pSaid = FindLine(options, aTopic, aInfo);
    if (!pSaid && CanSay(pTopic, pInfo))
    {
        pSaid = Build(*pManager, pTopic, pInfo);
        if (!pSaid)
            return false;
        if (cTopicOption != options.end())
            *cTopicOption = pSaid;
        else
            options.push_back(pSaid);
        SetList(*pManager, options);
    }
    else if (!pSaid && cTopicOption != options.end())
        pSaid = *cTopicOption;

    const int32_t cIndex = pSaid ? pManager->IndexOf(pSaid) : -1;
    if (cIndex < 0)
        return false;
    pManager->Pick(static_cast<uint32_t>(cIndex));
    return true;
}

void DialogueBridge::MarkSaid(uint32_t aInfo) noexcept
{
    // As TESTopicInfo::Begin (25616) does after the line's script.
    auto* pInfo = FormAs<TESTopicInfo>(aInfo);
    if (!pInfo)
        return;
    pInfo->MarkSaid();
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->RememberSaid(pInfo);
}

// ---- What a line leads to on the picker's game ----

void DialogueBridge::OpenTraining(uint32_t aTrainerFormId) noexcept
{
    if (Actor* pTrainer = FormAs<Actor>(aTrainerFormId))
        RealShowTraining(pTrainer);
}

void DialogueBridge::GainSpeechExperience(float aExperience) noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->GainSkillExperience(ActorValueInfo::kSpeechcraft, aExperience);
}
