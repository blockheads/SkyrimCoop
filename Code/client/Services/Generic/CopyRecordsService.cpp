#include <Services/CopyRecordsService.h>

#include <Components/LocalComponent.h>
#include <Events/DisconnectedEvent.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/Relayed.h>
#include <Services/TransportService.h>
#include <World.h>

CopyRecordsService::CopyRecordsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, CopyRecords& aRecords) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_records(aRecords)
{
    m_records.AskWith([this](uint32_t aId, RecordKind aKind) { Ask(aId, aKind); });
    m_records.SendWith([this](uint32_t aId, const RecordPart& acPart) {
        if (!m_transport.IsConnected())
            return;
        RecordRequest request;
        request.Id = aId;
        request.Part = acPart;
        m_transport.Send(request);
    });
    Listen<NotifyRecord, &CopyRecordsService::OnNotifyRecord>(m_connections, aDispatcher, this);
    Listen<NotifyRecordAsk, &CopyRecordsService::OnNotifyRecordAsk>(m_connections, aDispatcher, this);
    Listen<NotifyCharacterSpawn, &CopyRecordsService::OnCharacterSpawn>(m_connections, aDispatcher, this);
    Listen<NotifyRemoveCharacter, &CopyRecordsService::OnRemoveCharacter>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &CopyRecordsService::OnDisconnected>(m_connections, aDispatcher, this);
}

CopyRecordsService::~CopyRecordsService() noexcept
{
    m_records.SendWith({});
    m_records.AskWith({});
}

void CopyRecordsService::Ask(uint32_t aId, RecordKind aKind) const noexcept
{
    RecordAskRequest request;
    request.Id = aId;
    request.Kind = aKind;
    m_transport.Send(request);
}

void CopyRecordsService::OnNotifyRecord(const NotifyRecord& acMessage) noexcept
{
    m_records.Take(acMessage.Id, acMessage.Part);
}

void CopyRecordsService::OnNotifyRecordAsk(const NotifyRecordAsk& acMessage) noexcept
{
    // Only its owner answers: the host sent this to the owner it knew, which may have changed since.
    for (const auto cEntity : m_world.view<LocalComponent>())
        if (m_world.get<LocalComponent>(cEntity).Id == acMessage.Id)
        {
            m_records.DeclareAgain(acMessage.Id, acMessage.Kind);
            return;
        }
}

void CopyRecordsService::OnCharacterSpawn(const NotifyCharacterSpawn& acMessage) noexcept
{
    for (const RecordPart& cPart : acMessage.Record.Parts)
        m_records.Take(acMessage.SessionId, cPart);
}

void CopyRecordsService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept
{
    m_records.Forget(acMessage.SessionId);
}

void CopyRecordsService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_records.Clear();
}
