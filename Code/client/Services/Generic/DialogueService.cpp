#include <Services/DialogueService.h>

#include <Components.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/SharedDialogue.h>
#include <Bridges/IActorBridge.h>
#include <Services/HostService.h>
#include <Services/RunnerService.h>
#include <Services/TransportService.h>
#include <Bridges/IPlayerBridge.h>
#include <Utils.h>
#include <World.h>

DialogueService::DialogueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                 IDialogueBridge& aDialogueBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_dialogue(aDialogueBridge)
{
    m_dialogue.SetFilters({
        .Start = [this](uint32_t aActorFormId, bool aForceGreet) { return OnStartAsked(aActorFormId, aForceGreet); },
        .Talk = [this](uint32_t aActorFormId) { return OnTalk(aActorFormId); },
        .Pick = [this](uint32_t aIndex) { return OnPickAsked(aIndex); },
        // A peer's menu shows the host's lists only: its engine's own (after the copy's line) waits for the next one.
        .List = [this] { return !IsHost() && m_conversation && m_joined && m_sharedStep != m_step; },
        // A line's choices the host's player cannot answer stay open for the others' answers.
        .HoldChoices = [this] { return IsHost() && m_conversation; },
        .Training = [this](uint32_t aTrainerFormId) { return OnTrainingAsked(aTrainerFormId); },
        .SpeechExperience = [this](float aExperience) { return OnSpeechExperience(aExperience); },
    });
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&DialogueService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&DialogueService::OnDisconnected>(this);
    m_startConnection = aDispatcher.sink<NotifyDialogueStart>().connect<&DialogueService::OnStart>(this);
    m_openConnection = aDispatcher.sink<NotifyDialogueOpen>().connect<&DialogueService::OnOpen>(this);
    m_pickedConnection = aDispatcher.sink<NotifyDialoguePicked>().connect<&DialogueService::OnPicked>(this);
    m_listConnection = aDispatcher.sink<NotifyDialogueList>().connect<&DialogueService::OnList>(this);
    m_closedConnection = aDispatcher.sink<NotifyDialogueClosed>().connect<&DialogueService::OnClosed>(this);
    m_experienceConnection = aDispatcher.sink<NotifyDialogueExperience>().connect<&DialogueService::OnExperience>(this);
    m_refusedConnection = aDispatcher.sink<NotifyDialogueRefused>().connect<&DialogueService::OnRefused>(this);
    m_trainingConnection = aDispatcher.sink<NotifyDialogueTraining>().connect<&DialogueService::OnTraining>(this);
    m_lineConnection = aDispatcher.sink<NotifyDialogue>().connect<&DialogueService::OnLine>(this);
    m_subtitleConnection = aDispatcher.sink<NotifySubtitle>().connect<&DialogueService::OnSubtitle>(this);
}

bool DialogueService::OnTalk(uint32_t aActorFormId) noexcept
{
    if (!m_transport.IsOnline() || m_dialogue.IsMenuOpen())
        return false;
    // The host's conversation runs without its menu: talking to its NPC opens it.
    if (IsHost())
    {
        if (!m_conversation || aActorFormId != m_dialogue.GetSpeaker())
            return false;
        spdlog::info("Dialogue: the host joins conversation {} with {:X}", m_conversation, aActorFormId);
        if (!m_menuAsked)
            m_menuAsked = m_dialogue.ShowMenu();
        return true;
    }
    // A peer joins with its menu on its copy of the NPC.
    if (!m_conversation || CopyServerId(aActorFormId) != m_npcServerId)
        return false;
    spdlog::info("Dialogue: joining conversation {} (copy {:X})", m_conversation, aActorFormId);
    m_dialogue.Start(aActorFormId, true);
    return true;
}

bool DialogueService::OnStartAsked(uint32_t aActorFormId, bool aForceGreet) noexcept
{
    if (!m_transport.IsOnline())
        return true;
    if (IsHost())
    {
        // One conversation at a time: while its own runs without the menu, no other starts. An NPC greeting the
        // host's player (its AI runs here only) greets the party: every game opens its menu.
        const uint32_t cSpeaker = m_dialogue.GetSpeaker();
        if (!m_conversation || !cSpeaker || m_dialogue.IsMenuOpen() || aActorFormId == cSpeaker)
        {
            m_greeter = aForceGreet ? aActorFormId : 0;
            return true;
        }
        spdlog::info("Dialogue: {:X} would talk to the host, but the party talks to {:X}", aActorFormId, cSpeaker);
        return false;
    }
    // A copy's conversation is the host's: one under way with it is joined (OnTalk), any other is asked of the host,
    // whose game talks to the real NPC.
    const auto cServerId = CopyServerId(aActorFormId);
    if (!cServerId)
        return true;
    if (m_conversation && *cServerId == m_npcServerId)
        return true;
    spdlog::info("Dialogue: asking the host to talk to {:X} (copy {:X})", *cServerId, aActorFormId);
    m_asked = *cServerId;
    DialogueStartRequest request;
    request.NpcId = *cServerId;
    m_transport.Send(request);
    return false;
}

void DialogueService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsOnline())
        return;

    // Its speaker deleted while talking (the copy of an NPC left behind on travel): over, and its menu closes.
    if (m_dialogue.ClearOrphaned())
    {
        spdlog::info("Dialogue: the speaker of this game's conversation is gone, cleared");
        if (m_dialogue.IsMenuOpen())
            CloseMenu();
    }

    const bool cOpen = m_dialogue.IsMenuOpen();
    // The host's conversation runs with its menu or without it; a peer's is its menu on a copy.
    const uint32_t cSpeaker = cOpen || IsHost() ? m_dialogue.GetSpeaker() : 0;

    if (IsHost())
    {
        // Opened, or the host's player turned to another NPC: a new conversation for everyone.
        if (cSpeaker && cSpeaker != m_speaker)
        {
            m_conversation = 0;
            m_joined = false;
            if (const auto cServerId = Utils::GetServerIdByFormId(cSpeaker); cServerId && !CopyServerId(cSpeaker))
            {
                DialogueOpenedRequest opened;
                opened.NpcId = *cServerId;
                opened.Everyone = cSpeaker == m_greeter;
                m_transport.Send(opened);
            }
        }
        // Its conversation ended (a goodbye, the NPC walking off, combat): it ends for everyone.
        if (!cSpeaker && m_speaker && m_conversation)
        {
            spdlog::info("Dialogue: the host's engine ended conversation {}", m_conversation);
            DialogueCloseRequest close;
            close.Conversation = m_conversation;
            close.Ended = true;
            m_transport.Send(close);
            m_conversation = 0;
            m_joined = false;
        }
        // Its player leaving the menu leaves the conversation, which the bridge rule ends once nobody is in it.
        m_dialogue.SetMenuCloseEnds(!m_conversation);
    }
    else if (cOpen && !m_conversation && CopyServerId(cSpeaker))
        // A menu on a copy opens only for a conversation; one that ended before it opened closes.
        CloseMenu();
    else if (cOpen && !m_menuOpen && m_conversation)
        ReplayGreeting();

    // In it while this game's menu is open on its NPC; the peer who asked, from the open until its menu shows (or
    // its start came to nothing).
    const bool cIn = cOpen && m_conversation && Utils::GetServerIdByFormId(cSpeaker) == m_npcServerId;
    if (m_opening && (cOpen || m_dialogue.GetSpeaker() != Utils::GetFormIdByServerId(m_npcServerId)))
        m_opening = false;
    // A menu the UI never opened (its show dropped): the copy never ends its conversation, so this game leaves.
    constexpr uint32_t cMenuFrames = 60;
    if (m_opening && ++m_openingFrames > cMenuFrames)
    {
        spdlog::warn("Dialogue: the menu of conversation {} did not open, leaving it", m_conversation);
        m_opening = false;
        m_dialogue.End();
    }
    if (cIn && !m_joined)
    {
        DialogueJoinRequest join;
        join.Conversation = m_conversation;
        m_transport.Send(join);
    }
    else if (!cOpen && !m_opening && m_joined && m_conversation)
    {
        DialogueCloseRequest close;
        close.Conversation = m_conversation;
        m_transport.Send(close);
        m_own.clear();
        m_lines.reset();
    }
    m_joined = cIn || (m_joined && (cOpen || m_opening));

    if (m_trainer && !cOpen)
        m_dialogue.OpenTraining(std::exchange(m_trainer, 0));

    m_menuOpen = cOpen;
    m_menuAsked = m_menuAsked && cSpeaker && !cOpen;
    m_speaker = cSpeaker;
    m_closing = m_closing && cOpen;
    if (m_conversation && IsHost() && cSpeaker)
        SendOptions();
    else if (m_conversation && m_joined)
        ShowOwnLines();
}

void DialogueService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_conversation = 0;
    m_joined = false;
    m_opening = false;
    m_asked = 0;
    m_step = 0;
    m_talker = 0;
    m_menuOpen = false;
    m_menuAsked = false;
    m_closing = false;
    m_speaker = 0;
    m_sentOptions.clear();
    m_sentStep = 0;
    m_listsAtPick.reset();
    m_shared.clear();
    m_sharedStep = 0;
    m_own.clear();
    m_lines.reset();
    m_redraw = false;
    m_pickedStep = 0;
    m_trainer = 0;
    m_dialogue.SetMenuCloseEnds(true);
}

void DialogueService::OnStart(const NotifyDialogueStart& acMessage) noexcept
{
    if (!IsHost())
        return;
    DialogueStartFailedRequest failed;
    failed.NpcId = acMessage.NpcId;
    // Busy with a conversation of its own (one at a time).
    if (m_dialogue.GetSpeaker())
    {
        m_transport.Send(failed);
        return;
    }
    const uint32_t cFormId = Utils::GetFormIdByServerId(acMessage.NpcId);
    if (!cFormId || CopyServerId(cFormId))
    {
        spdlog::warn("Dialogue: a peer asked to talk to {:X}, which is not the host's here", acMessage.NpcId);
        m_transport.Send(failed);
        return;
    }
    // The host's player is the one talking, wherever it stands, without its menu: it joins by talking to the NPC.
    spdlog::info("Dialogue: a peer started a conversation with {:X}, run in the background", cFormId);
    if (!m_dialogue.Start(cFormId, false))
    {
        spdlog::info("Dialogue: the engine did not start the conversation with {:X}", cFormId);
        m_transport.Send(failed);
    }
}

void DialogueService::OnOpen(const NotifyDialogueOpen& acMessage) noexcept
{
    m_conversation = acMessage.Conversation;
    m_npcServerId = acMessage.NpcId;
    m_step = 1;
    m_talker = 0;
    m_sentOptions.clear();
    m_sentStep = 0;
    m_listsAtPick.reset();
    m_shared.clear();
    m_sharedStep = 0;
    m_own.clear();
    m_lines.reset();
    m_redraw = false;
    m_pickedStep = 0;
    m_joined = false;
    m_opening = false;
    // The player who asked for it is in it, every player when the NPC greeted the party: its menu opens on its
    // copy, or it leaves.
    if (IsHost() || (std::exchange(m_asked, 0) != acMessage.NpcId && !acMessage.Everyone))
        return;
    const uint32_t cCopy = Utils::GetFormIdByServerId(acMessage.NpcId);
    if (cCopy && m_dialogue.Start(cCopy, true))
    {
        spdlog::info("Dialogue: conversation {} opens on copy {:X}", m_conversation, cCopy);
        m_joined = true;
        m_opening = true;
        m_openingFrames = 0;
        return;
    }
    spdlog::warn("Dialogue: conversation {} could not open on copy {:X}", m_conversation, cCopy);
    if (cCopy && m_dialogue.GetSpeaker() == cCopy)
        m_dialogue.End();
    DialogueCloseRequest close;
    close.Conversation = m_conversation;
    m_transport.Send(close);
}

bool DialogueService::OnPickAsked(uint32_t aIndex) noexcept
{
    if (!m_transport.IsOnline() || !m_conversation || !m_joined)
        return false;
    // The host's own pick its game says; a peer's is the host's to say.
    const bool cSwallow = !IsHost();
    const auto cOptions = m_dialogue.GetOptions();
    // A click that is not sent: the menu takes a pick again.
    if (aIndex >= cOptions.size() || m_sharedStep != m_step)
    {
        if (aIndex < cOptions.size())
            spdlog::warn("Dialogue: the pick of {:X} came before the list of step {}", cOptions[aIndex].Topic, m_step);
        if (cSwallow)
            m_redraw = true;
        return cSwallow;
    }
    // Its menu's option: the topic with its own player's line (its Speech, its perks, what it wears).
    auto& modSystem = m_world.GetModSystem();
    DialoguePickRequest pick;
    pick.Conversation = m_conversation;
    pick.Step = m_step;
    modSystem.GetServerModId(cOptions[aIndex].Topic, pick.Topic);
    modSystem.GetServerModId(cOptions[aIndex].Info, pick.Info);
    pick.Text = cOptions[aIndex].Text.c_str();
    m_transport.Send(pick);
    m_pickedStep = m_step;
    if (IsHost())
        m_listsAtPick = m_dialogue.GetListsShown();
    return cSwallow;
}

void DialogueService::OnList(const NotifyDialogueList& acMessage) noexcept
{
    if (acMessage.Conversation != m_conversation)
        return;
    // A peer's menu shows each new step's list, even one its own engine has built already (its own show waits for
    // the host's), and the same step's again after its pick was not said.
    m_redraw = m_redraw || (!IsHost() && (acMessage.Step != m_sharedStep || acMessage.Step == m_pickedStep));
    m_shared = acMessage.Options;
    m_sharedStep = acMessage.Step;
    m_sharedChoices = acMessage.Choices;
    m_lines.reset();
}

void DialogueService::OnPicked(const NotifyDialoguePicked& acMessage) noexcept
{
    if (acMessage.Conversation != m_conversation)
        return;
    m_step = acMessage.Step;
    const bool cOwnPick = acMessage.PlayerId == m_transport.GetLocalPlayerId();
    if (IsHost())
        m_talker = cOwnPick ? 0 : acMessage.PlayerId;
    // The host's game says it, unless its own menu did; the other menus wait for the next list, and a peer's game
    // marks the line said as the host's does (a say-once line, or one within its reset hours, is not offered again).
    auto& modSystem = m_world.GetModSystem();
    const uint32_t cTopic = modSystem.GetGameId(acMessage.Topic);
    const uint32_t cInfo = modSystem.GetGameId(acMessage.Info);
    if (!IsHost())
        m_dialogue.MarkSaid(cInfo);
    if (!IsHost() || cOwnPick || !m_dialogue.GetSpeaker())
        return;
    m_listsAtPick = m_dialogue.GetListsShown();
    if (!m_dialogue.PickTopic(cTopic, cInfo))
    {
        // Nothing said: its list as it stands is the next step's.
        spdlog::warn("Dialogue: picked topic {:X} with info {:X} cannot be said here", cTopic, cInfo);
        m_listsAtPick.reset();
        return;
    }
    spdlog::info("Dialogue: player {} picked topic {:X} (info {:X})", acMessage.PlayerId, cTopic, cInfo);
}

void DialogueService::OnClosed(const NotifyDialogueClosed& acMessage) noexcept
{
    if (acMessage.Conversation != m_conversation)
        return;
    m_conversation = 0;
    m_joined = false;
    m_opening = false;
    if (IsHost())
        m_dialogue.SetMenuCloseEnds(true);
    if (m_dialogue.IsMenuOpen())
        CloseMenu();
    else if (IsHost() && m_dialogue.GetSpeaker())
        m_dialogue.End();
}

bool DialogueService::OnSpeechExperience(float aExperience) noexcept
{
    if (!m_talker)
        return false;
    m_world.GetRunner().Queue([this, aExperience] {
        DialogueExperienceRequest request;
        request.Experience = aExperience;
        m_transport.Send(request);
    });
    return true;
}

void DialogueService::OnExperience(const NotifyDialogueExperience& acMessage) noexcept
{
    m_dialogue.GainSpeechExperience(acMessage.Experience);
}

void DialogueService::OnRefused(const NotifyDialogueRefused& acMessage) noexcept
{
    if (m_asked == acMessage.NpcId)
        m_asked = 0;
}

bool DialogueService::OnTrainingAsked(uint32_t aTrainerFormId) noexcept
{
    if (!m_talker)
        return false;
    // Another player's pick: it trains on its own game (its gold, its skills); a trainer outside the conversation
    // opens here after all.
    m_world.GetRunner().Queue([this, aTrainerFormId] {
        const auto cServerId = Utils::GetServerIdByFormId(aTrainerFormId);
        if (!cServerId || *cServerId != m_npcServerId || CopyServerId(aTrainerFormId))
        {
            m_dialogue.OpenTraining(aTrainerFormId);
            return;
        }
        DialogueTrainingRequest request;
        request.NpcId = *cServerId;
        m_transport.Send(request);
    });
    return true;
}

void DialogueService::OnTraining(const NotifyDialogueTraining& acMessage) noexcept
{
    const uint32_t cTrainer = Utils::GetFormIdByServerId(acMessage.NpcId);
    if (!cTrainer)
    {
        spdlog::warn("Dialogue: training with {:X}, which is not here", acMessage.NpcId);
        return;
    }
    // The picker leaves the conversation for the training menu, which opens once its dialogue menu has closed.
    spdlog::info("Dialogue: training with {:X}", cTrainer);
    m_trainer = cTrainer;
    if (m_dialogue.IsMenuOpen())
        CloseMenu();
}

void DialogueService::CloseMenu() noexcept
{
    if (std::exchange(m_closing, true))
        return;
    m_dialogue.CloseMenu();
}

void DialogueService::OnLine(const NotifyDialogue& acMessage) noexcept
{
    m_lastLine = {acMessage.ServerId, acMessage.SoundFilename.c_str(), 0, std::chrono::steady_clock::now()};
}

void DialogueService::OnSubtitle(const NotifySubtitle& acMessage) noexcept
{
    m_lastSubtitle = {acMessage.ServerId, acMessage.Text.c_str(), acMessage.TopicFormId, std::chrono::steady_clock::now()};
}

void DialogueService::ReplayGreeting() noexcept
{
    constexpr auto cRecent = std::chrono::seconds(3);
    const auto cNow = std::chrono::steady_clock::now();
    const uint32_t cCopy = Utils::GetFormIdByServerId(m_npcServerId);
    if (!cCopy)
        return;
    auto& actors = m_world.GetActorBridge();
    if (m_lastLine.ServerId == m_npcServerId && cNow - m_lastLine.At < cRecent)
        actors.ActorSpeakSound(cCopy, m_lastLine.Text);
    if (m_lastSubtitle.ServerId == m_npcServerId && cNow - m_lastSubtitle.At < cRecent)
        actors.ActorShowSubtitle(cCopy, m_lastSubtitle.Text, m_lastSubtitle.TopicInfo, false);
}

void DialogueService::SendOptions() noexcept
{
    // After a pick, the step's list is the next one the engine shows, once the NPC has answered.
    if (m_listsAtPick && m_dialogue.GetListsShown() == *m_listsAtPick)
        return;
    m_listsAtPick.reset();
    auto options = m_dialogue.GetOptions();
    std::erase_if(options, [](const IDialogueBridge::Option& acOption) { return acOption.Added; });
    // A line's choices: every one, in order, those its player gets no line for without one (another player's
    // may get one: each game judges its own).
    if (m_dialogue.ShowsChoices())
    {
        auto choices = m_dialogue.GetChoices();
        for (auto& choice : choices)
            if (const auto cOwn = std::find_if(options.begin(), options.end(),
                                               [&choice](const IDialogueBridge::Option& acOption) { return acOption.Topic == choice.Topic; });
                cOwn != options.end())
                choice = *cOwn;
        for (const auto& cOption : options)
            if (std::none_of(choices.begin(), choices.end(), [&cOption](const IDialogueBridge::Option& acChoice) { return acChoice.Topic == cOption.Topic; }))
                choices.push_back(cOption);
        options = std::move(choices);
    }
    if (m_sentStep == m_step && options == m_sentOptions)
        return;

    auto& modSystem = m_world.GetModSystem();
    DialogueOptionsRequest request;
    request.Conversation = m_conversation;
    request.Step = m_step;
    request.Choices = m_dialogue.ShowsChoices();
    for (const auto& cOption : options)
    {
        DialogueOption& option = request.Options.emplace_back();
        modSystem.GetServerModId(cOption.Topic, option.Topic);
        modSystem.GetServerModId(cOption.Info, option.Info);
        modSystem.GetServerModId(cOption.Quest, option.Quest);
        option.Text = cOption.Text.c_str();
    }
    m_transport.Send(request);
    m_sentOptions = std::move(options);
    m_sentStep = m_step;
}

void DialogueService::ShowOwnLines() noexcept
{
    // Its engine's own options, built as its menu opened on the copy (and again if it builds them again).
    auto own = m_dialogue.GetOptions();
    std::erase_if(own, [](const IDialogueBridge::Option& acOption) { return acOption.Added; });
    if (!own.empty() && own != m_own)
    {
        m_own = std::move(own);
        m_lines.reset();
    }
    if (m_sharedStep != m_step)
        return;
    // Worked out once per list: a random topic's line would change at each look.
    if (!m_lines)
        m_lines = OwnLines();
    // A line's choices its player has no line for: it leaves, as the game ends it alone.
    if (m_sharedChoices && m_lines->empty())
    {
        CloseMenu();
        return;
    }
    if (!m_dialogue.ShowOptions(*m_lines) && m_redraw)
        m_dialogue.Redraw();
    m_redraw = false;
}

std::vector<IDialogueBridge::Option> DialogueService::OwnLines() const noexcept
{
    auto& modSystem = m_world.GetModSystem();
    std::vector<IDialogueBridge::Option> lines;
    // The host's topics with this player's lines: one it gets no line for is not its to say. A topic whose quest
    // does not run here is the host's world: the host's line.
    for (const DialogueOption& cShared : m_shared)
    {
        IDialogueBridge::Option line;
        line.Topic = modSystem.GetGameId(cShared.Topic);
        const std::optional<uint32_t> cOwn = m_dialogue.GetLine(line.Topic);
        line.Info = cOwn ? *cOwn : modSystem.GetGameId(cShared.Info);
        if (line.Topic && line.Info)
            lines.push_back(std::move(line));
    }
    // Then its own topics the host lacks, but not among a line's choices: nothing else is said there.
    if (m_sharedChoices)
        return lines;
    for (const IDialogueBridge::Option& cOwn : m_own)
    {
        const bool cListed = std::any_of(lines.begin(), lines.end(),
                                         [&cOwn](const IDialogueBridge::Option& acLine) { return acLine.Topic == cOwn.Topic; });
        if (!cListed)
            lines.push_back({cOwn.Topic, cOwn.Info, cOwn.Quest, {}, false});
    }
    return lines;
}

bool DialogueService::IsHost() const noexcept
{
    return m_world.GetHostService().IsHosting();
}

std::optional<uint32_t> DialogueService::CopyServerId(uint32_t aFormId) const noexcept
{
    if (!aFormId)
        return std::nullopt;
    auto view = m_world.view<FormIdComponent, RemoteComponent>();
    for (const entt::entity cEntity : view)
        if (view.get<FormIdComponent>(cEntity).Id == aFormId)
            return view.get<RemoteComponent>(cEntity).Id;
    return std::nullopt;
}
