#include "QuestService.h"

#include <spdlog/spdlog.h>

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"
#include "protocol.h"

// Quest-related offsets (from TESQuest game struct)
// TESForm::formID at offset 0x14
static constexpr uint64_t cFormIdOffset = 0x14;

QuestService::QuestService(GameReader& aReader, TcpClient& aClient)
    : m_reader(aReader)
    , m_client(aClient)
{
    spdlog::info("QuestService initialized (log-only mode, network deferred)");
}

void QuestService::OnHookEvent(const HookEventPacket& aPkt)
{
    switch (static_cast<HookOpcode>(aPkt.header.opcode)) {
    case HOOK_PERFORM_ACTION: {
        // PerformAction may include quest-related actions
        // args[0] = process, args[1] = actor, args[2] = action
        if (aPkt.argCount < 3) break;

        uint64_t actorPtr = aPkt.args[1];
        uint64_t actionPtr = aPkt.args[2];

        // Read actor formId for logging
        uint32_t actorFormId = m_reader.ReadFormId(actorPtr);

        // Log the quest-related action event
        spdlog::info("QuestService: PerformAction actor={:#x} action={:#x}",
                     actorFormId, actionPtr);
        m_lastQuestEventCount++;
        break;
    }
    case HOOK_ACTIVATE: {
        // Activation events can trigger quest stages
        if (aPkt.argCount < 2) break;

        uint64_t targetPtr = aPkt.args[0];
        uint64_t activatorPtr = aPkt.args[1];

        uint32_t targetFormId = m_reader.ReadFormId(targetPtr);
        uint32_t activatorFormId = m_reader.ReadFormId(activatorPtr);

        spdlog::debug("QuestService: Activate target={:#x} activator={:#x}",
                      targetFormId, activatorFormId);
        break;
    }
    default:
        break;
    }
}

void QuestService::Update(float /*aDeltaTime*/)
{
    // Quest state is event-driven, not polled.
    // No periodic work needed until network transport is wired.
}
