#include "QuestService.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "json_protocol.h"

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"

// Quest-related offsets (from TESQuest game struct)
// TESForm::formID at offset 0x14
static constexpr uint64_t cFormIdOffset = 0x14;

QuestService::QuestService(GameReader& aReader, TcpClient& aClient)
    : m_reader(aReader)
    , m_client(aClient)
{
    spdlog::info("QuestService initialized (log-only mode, network deferred)");
}

void QuestService::OnHookEvent(const nlohmann::json& aMsg)
{
    // QuestService currently has no dedicated hook ops routed to it from main.cpp.
    // This handler is a placeholder for future quest-related ops (e.g., quest stage
    // change, objective completion). When quest hooks are added to the relay DLL,
    // they will be dispatched here using the same JSON pattern as other services.
    const auto& op = aMsg[JsonField::kOp].get_ref<const std::string&>();
    spdlog::debug("QuestService: OnHookEvent op={}", op);
}

void QuestService::Update(float /*aDeltaTime*/)
{
    // Quest state is event-driven, not polled.
    // No periodic work needed until network transport is wired.
}
