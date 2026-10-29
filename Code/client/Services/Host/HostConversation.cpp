#include <Services/Host/HostConversation.h>

#include <Messages/SharedDialogue.h>
#include <Components.h>
#include <Services/RunnerService.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>
#include <FormIds.h>

HostConversation::HostConversation(World& aWorld, entt::dispatcher& aDispatcher, DialogueService& aDialogue) noexcept
    : m_world(aWorld)
    , m_dialogue(aDialogue)
{
    Listen<NotifyDialogueStart, &HostConversation::OnStart>(m_connections, aDispatcher, this);
    m_dialogue.SetRole(this);
}

HostConversation::~HostConversation() noexcept
{
    m_dialogue.SetRole(nullptr);
}

bool HostConversation::OnStartAsked(uint32_t aActorFormId, bool aForceGreet) noexcept
{
    // One conversation at a time: while its own runs without the menu, no other starts. An NPC greeting this game's
    // player (its AI runs here only) greets the party: every game opens its menu.
    auto& bridge = m_dialogue.Bridge();
    const uint32_t cSpeaker = bridge.GetSpeaker();
    if (!m_dialogue.Conversation() || !cSpeaker || bridge.IsMenuOpen() || aActorFormId == cSpeaker)
    {
        m_greeter = aForceGreet ? aActorFormId : 0;
        return true;
    }
    spdlog::info("Dialogue: {:X} would talk to the host, but the party talks to {:X}", aActorFormId, cSpeaker);
    return false;
}

bool HostConversation::OnTalk(uint32_t aActorFormId) noexcept
{
    // The conversation runs without its menu: talking to its NPC opens it.
    auto& bridge = m_dialogue.Bridge();
    if (bridge.IsMenuOpen() || !m_dialogue.Conversation() || aActorFormId != bridge.GetSpeaker())
        return false;
    spdlog::info("Dialogue: the host joins conversation {} with {:X}", m_dialogue.Conversation(), aActorFormId);
    if (!m_menuAsked)
        m_menuAsked = bridge.ShowMenu();
    return true;
}

uint32_t HostConversation::LineFor(uint32_t aSpeakerFormId) noexcept
{
    return aSpeakerFormId == m_pickerSpeaker ? m_picker.load() : 0;
}

void HostConversation::OnStart(const NotifyDialogueStart& acMessage) noexcept
{
    auto& bridge = m_dialogue.Bridge();
    DialogueStartFailedRequest failed;
    failed.NpcId = acMessage.NpcId;
    // Busy with a conversation of its own (one at a time).
    if (bridge.GetSpeaker())
    {
        m_dialogue.Transport().Send(failed);
        return;
    }
    const uint32_t cFormId = Utils::GetFormIdBySessionId(acMessage.NpcId);
    if (!cFormId || m_dialogue.CopySessionId(cFormId))
    {
        spdlog::warn("Dialogue: a guest asked to talk to {:X}, which is not the host's here", acMessage.NpcId);
        m_dialogue.Transport().Send(failed);
        return;
    }
    // This game's player is the one talking, wherever it stands, without its menu: it joins by talking to the NPC.
    spdlog::info("Dialogue: a guest started a conversation with {:X}, run in the background", cFormId);
    if (!bridge.Start(cFormId, false))
    {
        spdlog::info("Dialogue: the engine did not start the conversation with {:X}", cFormId);
        m_dialogue.Transport().Send(failed);
    }
}

std::optional<std::vector<IDialogueBridge::Option>> HostConversation::OnListBuilt() noexcept
{
    // Native arrest greetings can rebuild their choices before anyone picks.
    if (m_dialogue.Conversation())
        m_awaitingList = true;
    m_world.GetRunner().Queue([this] {
        if (std::exchange(m_awaitingList, false))
            SendOptions();
    });
    return std::nullopt;
}

std::vector<uint32_t> HostConversation::Judged() noexcept
{
    std::scoped_lock lock(m_judgedLock);
    return m_judged;
}

void HostConversation::OnOpen(const NotifyDialogueOpen&) noexcept
{
    m_picker = 0;
    m_awaitingList = false;
    SendOptions();
}

void HostConversation::OnPicked(const NotifyDialoguePicked& acMessage, uint32_t aTopic, uint32_t aInfo, bool aOwnLine) noexcept
{
    auto& bridge = m_dialogue.Bridge();
    // Before its line begins: its script runs for the picker, the host's own player (0x14) or a guest's character.
    m_pickerSpeaker = bridge.GetSpeaker();
    m_picker = aOwnLine ? 0x14 : CharacterOf(acMessage.PlayerId);
    m_awaitingList = true;
    if (!bridge.GetSpeaker())
        return;
    if (!bridge.PickOption(aTopic, aInfo))
    {
        // Nothing said: the engine's last list is the next step's.
        spdlog::warn("Dialogue: picked topic {:X} with info {:X} is not in the list here", aTopic, aInfo);
        m_awaitingList = false;
        ResendOptions();
        return;
    }
    spdlog::info("Dialogue: player {} picked topic {:X} (info {:X})", acMessage.PlayerId, aTopic, aInfo);
}

void HostConversation::OnClosed() noexcept
{
    auto& bridge = m_dialogue.Bridge();
    bridge.SetMenuCloseEnds(true);
    if (!bridge.IsMenuOpen() && bridge.GetSpeaker())
        bridge.End();
}

void HostConversation::OnUpdate(bool aMenuOpen, uint32_t aSpeaker) noexcept
{
    {
        std::vector<uint32_t> judged;
        for (const auto& [cPlayer, cCharacter] : OtherPlayers())
            judged.push_back(cCharacter);
        std::scoped_lock lock(m_judgedLock);
        m_judged = std::move(judged);
    }
    auto& transport = m_dialogue.Transport();
    // Opened, or this game's player turned to another NPC: a new conversation for everyone.
    if (aSpeaker && aSpeaker != m_speaker)
    {
        m_dialogue.Restart();
        if (const auto cSessionId = Utils::GetSessionIdByFormId(aSpeaker); cSessionId && !m_dialogue.CopySessionId(aSpeaker))
        {
            DialogueOpenedRequest opened;
            opened.NpcId = *cSessionId;
            opened.Everyone = aSpeaker == m_greeter;
            opened.Greeting = ToSession(m_dialogue.Bridge().GetGreeting()).value_or(GameId{});
            transport.Send(opened);
        }
    }
    // Its conversation ended (a goodbye, the NPC walking off, combat): it ends for everyone.
    if (!aSpeaker && m_speaker && m_dialogue.Conversation())
    {
        spdlog::info("Dialogue: the host's engine ended conversation {}", m_dialogue.Conversation());
        DialogueCloseRequest close;
        close.Conversation = m_dialogue.Conversation();
        close.Ended = true;
        transport.Send(close);
        m_dialogue.Restart();
    }
    // Its player leaving the menu leaves the conversation, which the host's rule ends once nobody is in it.
    m_dialogue.Bridge().SetMenuCloseEnds(!m_dialogue.Conversation());
    m_menuAsked = m_menuAsked && aSpeaker && !aMenuOpen;
    m_speaker = aSpeaker;
}

uint32_t HostConversation::CharacterOf(uint32_t aPlayerId) const noexcept
{
    auto view = m_world.view<PlayerComponent, FormIdComponent>();
    for (const entt::entity cEntity : view)
        if (view.get<PlayerComponent>(cEntity).Id == aPlayerId)
            return view.get<FormIdComponent>(cEntity).Id;
    return 0;
}

std::vector<std::pair<uint32_t, uint32_t>> HostConversation::OtherPlayers() const noexcept
{
    std::vector<std::pair<uint32_t, uint32_t>> players;
    auto view = m_world.view<PlayerComponent, FormIdComponent>();
    for (const entt::entity cEntity : view)
        if (const uint32_t cCharacter = view.get<FormIdComponent>(cEntity).Id; cCharacter && cCharacter != 0x14)
            players.emplace_back(view.get<PlayerComponent>(cEntity).Id, cCharacter);
    return players;
}

void HostConversation::ResendOptions() noexcept
{
    if (!m_dialogue.Conversation() || !m_dialogue.Bridge().GetSpeaker())
        return;
    m_built.Conversation = m_dialogue.Conversation();
    m_built.Step = m_dialogue.Step();
    m_dialogue.Transport().Send(m_built);
}

void HostConversation::SendOptions() noexcept
{
    auto& bridge = m_dialogue.Bridge();
    if (!m_dialogue.Conversation() || !bridge.GetSpeaker())
        return;

    DialogueOptionsRequest request;
    request.Conversation = m_dialogue.Conversation();
    request.Step = m_dialogue.Step();
    request.Choices = bridge.IsChoices();
    const auto cToMessage = [](const std::vector<IDialogueBridge::Option>& acOptions) {
        DialogueOptions options;
        for (const auto& cOption : acOptions)
        {
            DialogueOption& option = options.emplace_back();
            option.Topic = ToSession(cOption.Topic).value_or(GameId{});
            option.Info = ToSession(cOption.Info).value_or(GameId{});
            if (cOption.Quest)
                option.Quest = ToSession(cOption.Quest).value_or(GameId{});
            option.Text = cOption.Text;
        }
        return options;
    };
    request.Options = cToMessage(bridge.GetOptions());
    // Each other player's own list, judged for its character in the same build (one not judged yet gets the host's).
    for (const auto& [cPlayer, cCharacter] : OtherPlayers())
        if (const auto cOwn = bridge.GetOptionsFor(cCharacter))
            request.Players.push_back({cPlayer, cToMessage(*cOwn)});
    spdlog::info("Dialogue: step {} of conversation {}: {} options{}, {} players' own", request.Step, request.Conversation,
                 request.Options.size(), request.Choices ? ", a line's choices" : "", request.Players.size());
    m_dialogue.Transport().Send(request);
    m_built = std::move(request);
}
