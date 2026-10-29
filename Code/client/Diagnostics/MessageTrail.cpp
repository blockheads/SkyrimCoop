#include "MessageTrail.h"

#include <chrono>
#include <format>

namespace Diagnostics
{
std::string FormatMessages(const std::vector<MessageEntry>& acEntries)
{
    std::string text;
    for (const auto& cEntry : acEntries)
        text += std::format("{} t={} {} {} peer={:x} {}B\n", cEntry.TimeMs, cEntry.SessionTick, static_cast<char>(cEntry.Way), cEntry.Name,
                            cEntry.Peer, cEntry.Size);
    return text;
}

MessageTrail& Messages() noexcept
{
    static MessageTrail s_trail(100000, 5 * 60 * 1000);
    return s_trail;
}

void RecordMessage(uint64_t aSessionTick, MessageWay aWay, const char* acpName, uint32_t aPeer, uint32_t aSize) noexcept
{
    const auto cMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    Messages().Add({cMs, aSessionTick, aWay, acpName, aPeer, aSize});
}
} // namespace Diagnostics
