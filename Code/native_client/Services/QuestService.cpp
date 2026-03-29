#include "QuestService.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "json_protocol.h"

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

void QuestService::OnHookEvent(const nlohmann::json& aMsg)
{
    // TODO(Plan-04): Implement JSON dispatch for QuestService
    const auto& op = aMsg.value(JsonField::kOp, std::string{});
    spdlog::debug("[QuestService] stub OnHookEvent: op={}", op);
}

void QuestService::Update(float /*aDeltaTime*/)
{
    // Quest state is event-driven, not polled.
    // No periodic work needed until network transport is wired.
}
