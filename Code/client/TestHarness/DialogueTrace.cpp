#include "DialogueTrace.h"
#include "EngineCalls.h"

#include <Components.h>
#include <World.h>

#include <chrono>
#include <utility>

namespace TestHarness
{
namespace
{
// The session id the speaker goes by in every game (-1: none), as the status's world readout pairs actors.
int64_t SessionOf(uint32_t aFormId)
{
    auto& world = World::Get();
    for (const auto cEntity : world.view<FormIdComponent>())
    {
        if (world.get<FormIdComponent>(cEntity).Id != aFormId)
            continue;
        if (const auto* pLocal = world.try_get<LocalComponent>(cEntity))
            return pLocal->Id;
        if (const auto* pRemote = world.try_get<RemoteComponent>(cEntity))
            return pRemote->Id;
    }
    return -1;
}
} // namespace

nlohmann::json DialogueTrace::Take()
{
    return std::exchange(m_entries, nlohmann::json::array());
}

void DialogueTrace::Poll()
{
    const bool cMenu = EngineCalls::IsMenuOpen("Dialogue Menu");
    if (!cMenu && m_last.is_null())
        return;
    nlohmann::json state = {{"menu", false}};
    if (cMenu)
    {
        const EngineCalls::DialogueState cState = EngineCalls::GetDialogue();
        nlohmann::json options = nlohmann::json::array();
        for (const auto& cOption : cState.Options)
            options.push_back({cOption.Topic, cOption.Info, cOption.Text});
        state = {{"menu", true},
                 {"speaker", SessionOf(cState.Speaker)},
                 {"shown", cState.SwfState == 1},
                 {"options", options},
                 {"cursor", cState.CursorIndex},
                 {"info", cState.CurrentInfo}};
    }
    if (state == m_last)
        return;
    m_last = cMenu ? state : nlohmann::json();
    state["t"] = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    // A test whose conversation is never taken stays bounded.
    if (m_entries.size() >= 4096)
        m_entries.erase(m_entries.begin());
    m_entries.push_back(std::move(state));
}
} // namespace TestHarness
