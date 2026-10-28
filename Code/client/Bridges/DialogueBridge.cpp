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
#include <Papyrus/GuestScripts.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <TESObjectREFR.h>
#include <VersionDb.h>

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
    // The option with this line, else the topic's (the list built again since: a topic with random lines).
    const std::vector<EngineOption*> cOptions = pManager->Options();
    const auto cIsTopic = [aTopic](const EngineOption* apOption) { return TopicOf(*apOption) == aTopic; };
    auto it = std::ranges::find_if(cOptions, [&](const EngineOption* apOption) { return cIsTopic(apOption) && InfoOf(*apOption) == aInfo; });
    if (it == cOptions.end())
        it = std::ranges::find_if(cOptions, cIsTopic);
    if (it == cOptions.end())
        return false;
    pManager->Pick(static_cast<uint32_t>(it - cOptions.begin()));
    return true;
}
