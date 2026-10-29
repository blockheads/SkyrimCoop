#include <Bridges/DialogueBridge.h>

#include <Actor.h>
#include <Forms/TESForm.h>
#include <Forms/TESQuest.h>
#include <Forms/TESTopic.h>
#include <Forms/TESTopicInfo.h>
#include <Games/ActorExtension.h>
#include <Games/Misc/MenuTopicManager.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Interface/FxDelegateArgs.h>
#include <Interface/MenuOpenCloseEvent.h>
#include <Interface/UI.h>
#include <Misc/JudgedFor.h>
#include <Papyrus/GuestScripts.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <TESObjectREFR.h>
#include <VersionDb.h>

#include <map>
#include <mutex>

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
// The options the open menu shows, when they came from another game (ShowList, the List filter).
std::vector<IDialogueBridge::Option> s_shownElsewhere;
// A list build's topics (AddTopic's), each with the line every other player's character gets for it (Judged).
struct Judgement
{
    uint32_t Topic{};
    std::map<uint32_t, IDialogueBridge::Option> Lines; // by character
};
thread_local const std::vector<uint32_t>* t_pJudged = nullptr;
thread_local std::vector<Judgement> t_judgements;
// Each character's own list from the last build (GetOptionsFor).
std::mutex s_judgedLock;
std::map<uint32_t, std::vector<IDialogueBridge::Option>> s_judged;

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

bool IsBartering() noexcept
{
    const UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen("BarterMenu");
}

bool IsMenuShown() noexcept
{
    const UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(kDialogueMenu);
}

// The list becomes these options, each built from its topic and line as AddTopic does once its conditions pass
// (the Dialogue constructor 35254), no conditions tested. False: the open menu shows these already.
bool ReplaceOptions(MenuTopicManager& aManager, const std::vector<IDialogueBridge::Option>& acOptions) noexcept
{
    const bool cShown = IsMenuShown() && s_shownElsewhere == acOptions;
    s_shownElsewhere = acOptions;
    std::vector<EngineOption*> options;
    for (const auto& cOption : acOptions)
    {
        auto* pTopic = FormAs<TESTopic>(cOption.Topic);
        auto* pInfo = FormAs<TESTopicInfo>(cOption.Info);
        if (EngineOption* pBuilt = pTopic && pInfo ? aManager.BuildOption(pTopic, pInfo) : nullptr)
        {
            using TSetText = bool(BSString*, const char*, uint32_t);
            static VersionDbPtr<TSetText> setText(439876);
            setText(&pBuilt->topicText, cOption.Text.c_str(), 0);
            options.push_back(pBuilt);
        }
        else
            spdlog::warn("Dialogue: option {:X} (info {:X}) cannot be shown here", cOption.Topic, cOption.Info);
    }
    aManager.SetOptions(options);
    return !cShown;
}

// A conversation without its menu ends where the menu's fill (51515) would end it: a goodbye said, or nothing left to
// say.
void EndWhenDone(MenuTopicManager& aManager) noexcept
{
    if (aManager.forceGoodbye || !aManager.HasOptions())
        aManager.shutMenu = true;
}

// ---- Hooks ----

// Actor::SetDialogueWithPlayer (37200, CommonLibSSE-NG vfunc 0x41): starts (aStart) or stops the player's
// conversation with the actor; activating an NPC and force greets come here.
using TSetDialogueWithPlayer = bool(Actor*, bool aStart, bool aForceGreet, TESTopicInfo* apTopic);
TSetDialogueWithPlayer* RealSetDialogueWithPlayer = nullptr;

bool HookSetDialogueWithPlayer(Actor* apThis, bool aStart, bool aForceGreet, TESTopicInfo* apTopic)
{
    if (aStart && s_filters.Start && !s_filters.Start(apThis->formID, aForceGreet))
        return false;
    return RealSetDialogueWithPlayer(apThis, aStart, aForceGreet, apTopic);
}

// TESTopicInfo::Begin (25616): a line starts (aStage 0) or ends (1), its speaker's: queues its fragment there (the
// topic info event, 53997) and marks it said. A copy's lines are its owner's: its owner's game runs them.
using TBeginTopicInfo = uint64_t(TESTopicInfo*, int32_t aStage, uint32_t* apSpeakerHandle, bool aAnySpeaker);
TBeginTopicInfo* RealBeginTopicInfo = nullptr;

uint64_t HookBeginTopicInfo(TESTopicInfo* apThis, int32_t aStage, uint32_t* apSpeakerHandle, bool aAnySpeaker)
{
    Actor* pSpeaker = apSpeakerHandle ? Cast<Actor>(TESObjectREFR::GetByHandle(*apSpeakerHandle)) : nullptr;
    if (pSpeaker && pSpeaker->GetExtension()->IsCopy())
        return 0;
    // Its fragment runs for the player the LineFor filter names (GuestScripts, rule 2).
    std::optional<GuestScripts::ScopedCause> cause;
    if (const uint32_t cFor = pSpeaker && s_filters.LineFor ? s_filters.LineFor(pSpeaker->formID) : 0)
        cause.emplace(cFor);
    return RealBeginTopicInfo(apThis, aStage, apSpeakerHandle, aAnySpeaker);
}

// TESNPC::Activate (24715, CommonLibSSE-NG TESBoundObject::Activate): the player talking to an NPC. Talking to its
// last speaker again can do nothing (a copy never leaves its conversation), so joins are decided here.
using TNpcActivate = bool(TESForm*, TESObjectREFR* apTarget, TESObjectREFR* apActivator, uint8_t aUnk1, TESForm* apObject,
                          int32_t aCount);
TNpcActivate* RealNpcActivate = nullptr;

bool HookNpcActivate(TESForm* apThis, TESObjectREFR* apTarget, TESObjectREFR* apActivator, uint8_t aUnk1, TESForm* apObject,
                     int32_t aCount)
{
    if (s_filters.Talk && apTarget && apActivator == PlayerCharacter::Get() && s_filters.Talk(apTarget->formID))
        return true;
    return RealNpcActivate(apThis, apTarget, apActivator, aUnk1, apObject, aCount);
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
    s_shownElsewhere.clear();
    if (s_startingHidden)
        return 0;
    s_hidden = false;
    return RealShowMenu(acpSpeakerName);
}

// Turns the player to the speaker (40496, PlayerCharacter; called only from 35282).
using TTurnToSpeaker = void(PlayerCharacter*);
TTurnToSpeaker* RealTurnToSpeaker = nullptr;

void HookTurnToSpeaker(PlayerCharacter* apThis)
{
    if (!s_startingHidden)
        RealTurnToSpeaker(apThis);
}

// The list build (35287): clears the list, then AddTopics the said line's choices, the speaker's exclusive branch, or
// its topics (35276). Each other player's character gets its own list from the same topics (AddTopic below): the menu's
// topics it has a line for, in the menu's order (the engine sorts it), then those only it has.
using TBuildList = uint64_t(MenuTopicManager*);
TBuildList* RealBuildList = nullptr;

uint64_t HookBuildList(MenuTopicManager* apThis)
{
    const std::vector<uint32_t> cCharacters = s_filters.Judged ? s_filters.Judged() : std::vector<uint32_t>{};
    t_judgements.clear();
    t_pJudged = cCharacters.empty() ? nullptr : &cCharacters;
    const uint64_t cResult = RealBuildList(apThis);
    t_pJudged = nullptr;

    std::map<uint32_t, std::vector<IDialogueBridge::Option>> judged;
    const std::vector<EngineOption*> cListed = apThis->Options();
    for (const uint32_t cCharacter : cCharacters)
    {
        auto& list = judged[cCharacter];
        std::vector<bool> used(t_judgements.size());
        const auto cTake = [&](size_t aCall) {
            const auto it = t_judgements[aCall].Lines.find(cCharacter);
            if (used[aCall] || it == t_judgements[aCall].Lines.end())
                return false;
            used[aCall] = true;
            list.push_back(it->second);
            return true;
        };
        for (const EngineOption* pListed : cListed)
            for (size_t i = 0; i < t_judgements.size(); ++i)
                if (t_judgements[i].Topic == TopicOf(*pListed) && cTake(i))
                    break;
        for (size_t i = 0; i < t_judgements.size(); ++i)
            cTake(i);
    }
    t_judgements.clear();
    std::scoped_lock lock(s_judgedLock);
    s_judged = std::move(judged);
    return cResult;
}

// MenuTopicManager::AddTopic (35303): the speaker's line for the topic to this game's player (TESTopic 25544, at
// +0x88), as an option in the list. In a build, the topic is judged for each other player's character too.
using TAddTopic = EngineOption*(MenuTopicManager*, TESTopic*, void* apPrevious, bool aFront);
TAddTopic* RealAddTopic = nullptr;

EngineOption* HookAddTopic(MenuTopicManager* apThis, TESTopic* apTopic, void* apPrevious, bool aFront)
{
    EngineOption* pResult = RealAddTopic(apThis, apTopic, apPrevious, aFront);
    TESObjectREFR* pSpeaker = t_pJudged && apTopic ? TESObjectREFR::GetByHandle(apThis->speaker) : nullptr;
    if (!pSpeaker)
        return pResult;
    Judgement& judgement = t_judgements.emplace_back();
    judgement.Topic = apTopic->formID;
    for (const uint32_t cCharacter : *t_pJudged)
    {
        auto* pCharacter = FormAs<TESObjectREFR>(cCharacter);
        TESTopicInfo* pLine = nullptr;
        if (pCharacter)
        {
            const JudgedFor::Scope cJudged(pCharacter);
            pLine = apTopic->LineFor(pSpeaker, pCharacter);
        }
        // Built as AddTopic builds it, for the text the menu would show (its tokens rendered); none without responses.
        if (EngineOption* pBuilt = pLine ? apThis->BuildOption(apTopic, pLine) : nullptr)
        {
            const char* cpText = pBuilt->topicText.AsAscii();
            judgement.Lines[cCharacter] = {apTopic->formID, pLine->formID, IdOf(pBuilt->parentQuest), cpText ? cpText : ""};
            MenuTopicManager::Destroy(pBuilt);
        }
    }
    return pResult;
}

// Asks the menu to show the list (52857), which the engine has just built (35287): after each line, and at the
// start. The menu's fill then ends the conversation when nothing is left to say or a goodbye was said (51515).
using TShowList = void(bool aNewList);
TShowList* RealShowList = nullptr;

void HookShowList(bool aNewList)
{
    MenuTopicManager* pManager = MenuTopicManager::Get();
    const auto cList = s_filters.List ? s_filters.List() : std::nullopt;
    if (cList && IsBartering())
        return;
    if (!cList)
        s_shownElsewhere.clear();
    // The open menu shows these already: filled again, it would take no pick for 750 ms.
    else if (pManager && !ReplaceOptions(*pManager, *cList))
        return;
    RealShowList(aNewList);
    if (pManager && s_hidden)
        EndWhenDone(*pManager);
}

// An open menu fills its SWF from the list again (51515), sliding it in as a line's choices (52857), and takes a pick
// 750 ms after its voice-ready call (52858), as after a line.
void RefillMenu(bool aChoices) noexcept
{
    using TVoiceReady = void();
    POINTER_SKYRIMSE(TVoiceReady, s_voiceReady, 52858);
    if (!IsMenuShown())
        return;
    RealShowList(aChoices);
    s_voiceReady.Get()();
}

// MenuTopicManager's menu close sink (35272): the dialogue menu closing ends the conversation (shutMenu).
using TMenuOpenClose = uint32_t(MenuTopicManager*, const MenuOpenCloseEvent* apEvent, void* apSource);
TMenuOpenClose* RealMenuOpenClose = nullptr;

uint32_t HookMenuOpenClose(MenuTopicManager* apThis, const MenuOpenCloseEvent* apEvent, void* apSource)
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
    return RealMenuOpenClose(apThis, apEvent, apSource);
}

Hooks::Initializer s_dialogueHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(37200, &RealSetDialogueWithPlayer, HookSetDialogueWithPlayer);
        HOOK_ID(25616, &RealBeginTopicInfo, HookBeginTopicInfo);
        HOOK_ID(24715, &RealNpcActivate, HookNpcActivate);
        HOOK_ID(51509, &RealTopicClicked, HookTopicClicked);
        HOOK_ID(52853, &RealShowMenu, HookShowMenu);
        HOOK_ID(40496, &RealTurnToSpeaker, HookTurnToSpeaker);
        HOOK_ID(52857, &RealShowList, HookShowList);
        HOOK_ID(35287, &RealBuildList, HookBuildList);
        HOOK_ID(35303, &RealAddTopic, HookAddTopic);
        HOOK_ID(35272, &RealMenuOpenClose, HookMenuOpenClose);
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
        options.push_back({TopicOf(*pListed), InfoOf(*pListed), IdOf(pListed->parentQuest), cpText ? cpText : ""});
    }
    return options;
}

std::optional<std::vector<IDialogueBridge::Option>> DialogueBridge::GetOptionsFor(uint32_t aCharacterFormId) const noexcept
{
    std::scoped_lock lock(s_judgedLock);
    const auto it = s_judged.find(aCharacterFormId);
    return it != s_judged.end() ? std::optional(it->second) : std::nullopt;
}

bool DialogueBridge::IsChoices() const noexcept
{
    const MenuTopicManager* pManager = Conversation();
    return pManager && pManager->rootTopicInfo;
}

// ---- Starting and ending it ----

uint32_t DialogueBridge::GetGreeting() const noexcept
{
    const auto* manager = Conversation();
    return manager ? IdOf(manager->currentTopicInfo) : 0;
}

bool DialogueBridge::Start(uint32_t aActorFormId, bool aShowMenu, uint32_t aGreeting) noexcept
{
    // Nobody talks while fighting or dead, as talking to them does; a start from code would run all the same.
    Actor* pActor = FormAs<Actor>(aActorFormId);
    if (!pActor || pActor->IsDead() || pActor->IsInCombat())
        return false;
    s_startingHidden = !aShowMenu;
    // Arrest greetings depend on alarm AI that copies do not run (37200).
    // An explicit host-selected INFO takes the native force-greet path without reselecting it.
    const bool cStarted = RealSetDialogueWithPlayer(pActor, true, aGreeting != 0, FormAs<TESTopicInfo>(aGreeting));
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
    RealTurnToSpeaker(PlayerCharacter::Get());
    return true;
}

void DialogueBridge::CloseMenu() noexcept
{
    // A kHide whose data is set plays the SWF's fade-out first (DialogueMenu::ProcessMessage 51506), whose CloseMenu
    // callback (51511) gives the HUD its subtitles back.
    UIMessageQueue* pQueue = UIMessageQueue::Get();
    BSUIMessageData* pData = pQueue && IsMenuShown() ? UIMessageQueue::CreateData() : nullptr;
    if (!pData)
        return;
    pData->data.b = true;
    pQueue->AddMessage(kDialogueMenu, UIMessage::kHide, pData);
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
    return true;
}

// ---- The list and picks ----

bool DialogueBridge::ShowList(const std::vector<Option>& acOptions, bool aChoices) noexcept
{
    // A refresh raises dialogue above the trader's menu and takes its input.
    if (IsBartering())
        return false;
    MenuTopicManager* pManager = Conversation();
    if (!pManager)
        return false;
    if (ReplaceOptions(*pManager, acOptions))
        RefillMenu(aChoices);
    return true;
}

bool DialogueBridge::PickOption(uint32_t aTopic, uint32_t aInfo) noexcept
{
    MenuTopicManager* pManager = Conversation();
    if (!pManager)
        return false;
    // The option with this line, else one built for it: a line judged for another player's character, or the
    // topic's other line since the list was built (a topic with random lines).
    std::vector<EngineOption*> options = pManager->Options();
    const auto it = std::ranges::find_if(options, [&](const EngineOption* apOption) {
        return TopicOf(*apOption) == aTopic && InfoOf(*apOption) == aInfo;
    });
    if (it != options.end())
    {
        pManager->Pick(static_cast<uint32_t>(it - options.begin()));
        return true;
    }
    EngineOption* pBuilt = pManager->BuildOption(FormAs<TESTopic>(aTopic), FormAs<TESTopicInfo>(aInfo));
    if (!pBuilt)
        return false;
    options.push_back(pBuilt);
    pManager->SetOptions(options);
    pManager->Pick(static_cast<uint32_t>(options.size() - 1));
    return true;
}
