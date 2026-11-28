#include <world/GuestWorldState.h>

#include <ports/IGuestEngine.h>
#include <physics/BodyService.h>
#include <Messages/WorldState.h>
#include <core/UpdateEvent.h>
#include <items/ContainerChangeEvent.h>
#include <world/LockChangeEvent.h>
#include <world/DestructionEvent.h>
#include <items/LooseItemEvents.h>
#include <scripts/StoryEvents.h>
#include <magic/SummonEvent.h>
#include <Messages/SummonRequest.h>
#include <Messages/Relayed.h>
#include <scripts/PlayerNatives.h>
#include <core/ScriptValues.h>
#include <scripts/StoryEventRules.h>
#include <Messages/CellStateRequest.h>
#include <Messages/ContainerChangeRequest.h>
#include <Messages/DroppedItems.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/ObjectStateRequest.h>
#include <Messages/StoryEventRequest.h>
#include <core/TransportService.h>
#include <world/WorldStateLog.h>
#include <core/World.h>
#include <core/FormIds.h>
#include <core/SessionIds.h>
#include <core/Profile.h>

#include <cstring>

GuestWorldState::GuestWorldState(World& aWorld, entt::dispatcher& aDispatcher, IGuestEngine& aEngine) noexcept
    : m_world(aWorld)
    , m_engine(aEngine)
{
    Listen<NotifyGlobals, &GuestWorldState::OnGlobals>(m_connections, aDispatcher, this);
    Listen<NotifyQuests, &GuestWorldState::OnQuests>(m_connections, aDispatcher, this);
    Listen<NotifyObjectChanges, &GuestWorldState::OnObjectChanges>(m_connections, aDispatcher, this);
    Listen<LockChangeEvent, &GuestWorldState::OnLockChange>(m_connections, aDispatcher, this);
    Listen<DestructionEvent, &GuestWorldState::OnDestruction>(m_connections, aDispatcher, this);
    Listen<ContainerChangeEvent, &GuestWorldState::OnContainerChange>(m_connections, aDispatcher, this);
    Listen<ItemDroppedEvent, &GuestWorldState::OnItemDropped>(m_connections, aDispatcher, this);
    Listen<NotifyItemDropped, &GuestWorldState::OnItemDroppedNotify>(m_connections, aDispatcher, this);
    Listen<ItemPickUpEvent, &GuestWorldState::OnItemPickUp>(m_connections, aDispatcher, this);
    Listen<NotifyPickUp, &GuestWorldState::OnPickUpNotify>(m_connections, aDispatcher, this);
    Listen<OwnStoryEvent, &GuestWorldState::OnStoryEvent>(m_connections, aDispatcher, this);
    Listen<SummonEvent, &GuestWorldState::OnSummon>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerNativeResult, &GuestWorldState::OnCallResult>(m_connections, aDispatcher, this);
    // this is what stops guests (clients connected to the host) from starting their own quests or running
    // their own world scripts: the host's game decides
    m_engine.SetDecidesWorld(false);
}

GuestWorldState::~GuestWorldState() noexcept
{
    // Left the session: the scripts still waiting on the host go on with None, answered or not.
    PlayerNatives::AnswerAll();
    m_engine.SetDecidesWorld(true);
}

void GuestWorldState::OnGlobals(const NotifyGlobals& acMessage) noexcept
{
    size_t missing = 0;
    for (const auto& cValue : acMessage.Values)
        if (!m_engine.SetGlobal(cValue))
            ++missing;
    spdlog::info("GuestWorldState: applied {} of the host's {} globals", acMessage.Values.size() - missing, acMessage.Values.size());
    if (missing)
        spdlog::warn("{}: {} of the host's {} globals are not in this game (or constant here)", __FUNCTION__, missing,
                     acMessage.Values.size());
}

void GuestWorldState::OnQuests(const NotifyQuests& acMessage) noexcept
{
    m_mainTasks.Queue([this, message = acMessage] {
        for (const auto& cQuest : message.Quests)
            m_engine.ApplyQuest(cQuest, message.Join);
        spdlog::info("GuestWorldState: applied the host's {} quests{}", message.Quests.size(), message.Join ? " (as a load)" : "");
    });
}

void GuestWorldState::OnObjectChanges(const NotifyObjectChanges& acMessage) noexcept
{
    m_mainTasks.Queue([this, message = acMessage] {
        // A cell's state arriving after this game left it: the next load asks again.
        if (message.Cell != GameId{} && !m_engine.IsCellAttached(message.Cell))
        {
            spdlog::info("GuestWorldState: the host's state of cell {:X} came after it unloaded here, left", message.Cell.LogFormat());
            return;
        }
        std::vector<ObjectChange> applied;
        for (const auto& cChange : message.Changes)
        {
            // A live move of a reference whose body is loaded here is the body stream's: one writer for its pose
            // (physics-authority-plan.md, piece 4). A cell's load, or a reference with no body here, takes the row.
            if (message.Cell == GameId{} && cChange.Type == ObjectChange::Kind::kMove &&
                (m_engine.Adopting(cChange.Object) || m_engine.HasLiveBody(cChange.Object)))
            {
                spdlog::debug("GuestWorldState: {} is its body's to place", WorldStateLog::Describe(cChange));
                continue;
            }
            // This game's player's own pull of a lever furniture: its engine played it (TESFurniture::Activate 17420).
            if (cChange.Type == ObjectChange::Kind::kAnimation && cChange.Value && cChange.Value == Utils::GetSessionIdByFormId(0x14))
                continue;
            // An animation, or a live change behind one of its object's still waiting: in the host's order.
            const uint64_t cObject = uint64_t{cChange.Object.ModId} << 32 | cChange.Object.BaseId;
            if (cChange.Type == ObjectChange::Kind::kAnimation || (message.Cell == GameId{} && m_animations.Waiting(cObject)))
            {
                m_animations.Play(cObject, cChange, std::chrono::steady_clock::now(), [this](const ObjectChange& acChange) { return ApplyInOrder(acChange); });
                continue;
            }
            if (m_engine.ApplyObjectChange(cChange, message.Cell != GameId{}))
                applied.push_back(cChange);
            else
                spdlog::warn("GuestWorldState: could not apply the host's {}: its object, activator or item is not in this game", WorldStateLog::Describe(cChange));
        }
        if (message.Cell != GameId{})
            spdlog::info("GuestWorldState: applied cell {:X}'s state, {} of {} changes: {}", message.Cell.LogFormat(), applied.size(),
                         message.Changes.size(), WorldStateLog::List(applied, &WorldStateLog::Describe, 16));
        else
            WorldStateLog::LogLive("GuestWorldState: applied the host's", applied);
    });
}

bool GuestWorldState::ApplyInOrder(const ObjectChange& acChange) noexcept
{
    const bool cTaken = m_engine.ApplyObjectChange(acChange, false);
    if (!cTaken && acChange.Type != ObjectChange::Kind::kAnimation)
        spdlog::warn("GuestWorldState: could not apply the host's {}: its object, activator or item is not in this game", WorldStateLog::Describe(acChange));
    return cTaken || acChange.Type != ObjectChange::Kind::kAnimation;
}

void GuestWorldState::AskCellState(uint32_t aCellFormId) noexcept
{
    const auto cRequest = m_engine.ReadCellChanges(aCellFormId);
    if (!cRequest)
        return;
    spdlog::info("GuestWorldState: cell {:X} loaded, asked for the host's state ({} changed here)", cRequest->Cell.LogFormat(), cRequest->Changed.size());
    m_world.GetTransport().Send(*cRequest);
}

void GuestWorldState::OnLockChange(const LockChangeEvent& acEvent) noexcept
{
    // The world's own (its scripts, a cell load) is the host's to decide (step 5).
    if (!acEvent.ByPlayer)
        return;
    const auto cId = ToSession(acEvent.FormId);
    if (!cId)
        return;
    LockChangeRequest request;
    request.Id = *cId;
    request.IsLocked = acEvent.IsLocked;
    request.LockLevel = acEvent.LockLevel;
    spdlog::info("GuestWorldState: proposed lock {:X} {} at {}", request.Id.LogFormat(), request.IsLocked ? "locked" : "unlocked", request.LockLevel);
    m_world.GetTransport().Send(request);
}

void GuestWorldState::OnDestruction(const DestructionEvent& acEvent) noexcept
{
    auto row = m_engine.ReadObjectState(acEvent.FormId, ObjectChange::Kind::kDestruction);
    if (!row)
        return;
    ObjectStateRequest request;
    request.Change = std::move(*row);
    spdlog::info("GuestWorldState: proposed {}", WorldStateLog::Describe(request.Change));
    m_world.GetTransport().Send(request);
}

void GuestWorldState::OnStoryEvent(const OwnStoryEvent& acEvent) noexcept
{
    auto event = m_engine.ReadStoryEvent(acEvent);
    if (!event)
        return;
    StoryEventRequest request;
    request.Event = std::move(*event);
    spdlog::info("GuestWorldState: story event {} sent to the host's story manager",
                 StoryEventRules::TypeName(request.Event.Type));
    m_world.GetTransport().Send(request);
}

void GuestWorldState::OnContainerChange(const ContainerChangeEvent& acEvent) noexcept
{
    // Actors' items are their owner's (InventoryService); a container's are proposed as its count now.
    const auto cChange = m_engine.ReadContainerItems(acEvent.ContainerId, acEvent.ItemId);
    if (!cChange)
        return;
    ContainerChangeRequest request;
    request.Container = cChange->Object;
    request.Item = cChange->Item;
    request.Count = cChange->Value;
    request.Stacks = cChange->Stacks;
    spdlog::info("GuestWorldState: proposed {}", WorldStateLog::Describe(*cChange));
    m_world.GetTransport().Send(request);
}

void GuestWorldState::OnItemDropped(const ItemDroppedEvent& acEvent) noexcept
{
    const auto cRequest = m_engine.ReadDrop(acEvent.ObjectId);
    if (!cRequest)
        return;
    spdlog::info("GuestWorldState: dropped {:X} ({} of {:X}), asked the host to make it", cRequest->Tag, cRequest->Item.Count,
                 cRequest->Item.BaseId.LogFormat());
    m_world.GetTransport().Send(*cRequest);
}

void GuestWorldState::OnItemDroppedNotify(const NotifyItemDropped& acMessage) noexcept
{
    if (acMessage.Object == GameId{})
        spdlog::warn("GuestWorldState: the host's game cannot make drop {:X}: it stays this game's own", acMessage.Tag);
    if (m_engine.AdoptDrop(acMessage.Tag, acMessage.Object))
        spdlog::info("GuestWorldState: the host made drop {:X} as {:X}", acMessage.Tag, acMessage.Object.LogFormat());
    else
        spdlog::warn("GuestWorldState: the host made drop {:X} as {:X}, no longer here", acMessage.Tag, acMessage.Object.LogFormat());
}

void GuestWorldState::OnSummon(const SummonEvent& acEvent) noexcept
{
    const auto cSpell = ToSession(acEvent.SpellId);
    const auto cEffect = ToSession(acEvent.EffectId);
    if (!cSpell || !cEffect)
    {
        spdlog::warn("GuestWorldState: summon {:X} is a created spell: not made", acEvent.SpellId);
        return;
    }
    SummonRequest request;
    request.SpellId = *cSpell;
    request.EffectId = *cEffect;
    request.Magnitude = acEvent.Magnitude;
    request.Location = acEvent.Location;
    request.DualCast = acEvent.DualCast;
    spdlog::info("GuestWorldState: summon {:X} asked of the host's game", acEvent.SpellId);
    m_world.GetTransport().Send(request);
}

void GuestWorldState::OnCallResult(const NotifyPlayerNativeResult& acMessage) noexcept
{
    if (!AnswerCall(acMessage.ReplyId, acMessage.Result))
    {
        spdlog::info("GuestWorldState: the host's answer to stack {} waits for its reference", acMessage.ReplyId);
        m_answers.push_back({acMessage.ReplyId, acMessage.Result, std::chrono::steady_clock::now()});
    }
}

bool GuestWorldState::AnswerCall(uint32_t aReplyId, const ScriptValue& acResult) noexcept
{
    // A form not here yet (an actor before its spawn, a created reference before its create row) waits.
    const auto cLocal = Utils::ToLocalValue(acResult);
    if (!cLocal)
        return false;
    spdlog::info("GuestWorldState: the host answered stack {}", aReplyId);
    PlayerNatives::Answer(aReplyId, *cLocal);
    return true;
}

void GuestWorldState::OnItemPickUp(const ItemPickUpEvent& acEvent) noexcept
{
    const auto cRequest = m_engine.ReadPickUp(acEvent.ObjectId);
    if (!cRequest)
        return;
    spdlog::info("GuestWorldState: asked the host for {:X} (tag {:X})", cRequest->Object.LogFormat(), cRequest->Tag);
    m_world.GetTransport().Send(*cRequest);
}

void GuestWorldState::OnPickUpNotify(const NotifyPickUp& acMessage) noexcept
{
    m_engine.PickUp(acMessage);
    spdlog::info("GuestWorldState: picked up {:X} (tag {:X}): {} of {:X}", acMessage.Object.LogFormat(), acMessage.Tag, acMessage.Item.Count,
                 acMessage.Item.BaseId.LogFormat());
}

void GuestWorldState::OnUpdate(const UpdateEvent&) noexcept
{
    PROFILE_ZONE("GuestWorldState::OnUpdate");
    // The host's created reference reaches this game in its next update's rows; one deleted first never does.
    constexpr auto cCreatedWait = std::chrono::seconds(5);
    std::erase_if(m_answers, [this, cNow = std::chrono::steady_clock::now(), cCreatedWait](const WaitingAnswer& acAnswer) {
        if (AnswerCall(acAnswer.ReplyId, acAnswer.Result))
            return true;
        if (cNow - acAnswer.Since < cCreatedWait)
            return false;
        spdlog::warn("GuestWorldState: the host's answer to stack {} names a reference that never came: None", acAnswer.ReplyId);
        PlayerNatives::Answer(acAnswer.ReplyId, std::monostate{});
        return true;
    });
    m_mainTasks.Queue([this] {
        m_engine.RetryQuestAliases();
        m_animations.Update(std::chrono::steady_clock::now(), [this](const ObjectChange& acChange) { return ApplyInOrder(acChange); });
    });
    auto cells = m_engine.AttachedCells();
    for (const uint32_t cCell : cells)
        if (std::ranges::find(m_cells, cCell) == m_cells.end())
            AskCellState(cCell);
    m_cells = std::move(cells);
}
