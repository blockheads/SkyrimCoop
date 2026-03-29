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
        // DLL arg layout: [processPtr, actorPtr, actorFormId, actionPtr, actionFormId]
        if (aPkt.argCount < 5) break;

        uint32_t actorFormId = static_cast<uint32_t>(aPkt.args[2]);
        uint32_t actionFormId = static_cast<uint32_t>(aPkt.args[4]);

        // Log the quest-related action event
        spdlog::info("QuestService: PerformAction actor={:#x} action={:#x}",
                     actorFormId, actionFormId);
        m_lastQuestEventCount++;
        break;
    }
    case HOOK_ACTIVATE: {
        // DLL arg layout: [thisPtr, thisFormId, activatorPtr, activatorFormId, objPtr, countU64]
        if (aPkt.argCount < 4) break;

        uint32_t targetFormId = static_cast<uint32_t>(aPkt.args[1]);
        uint32_t activatorFormId = static_cast<uint32_t>(aPkt.args[3]);

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
