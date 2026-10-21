#include <Services/Guest/GuestWorldState.h>

#include <Bridges/GuestEngine.h>
#include <Messages/WorldState.h>

GuestWorldState::GuestWorldState(entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_engine(aEngine)
{
    m_globalsConnection = aDispatcher.sink<NotifyGlobals>().connect<&GuestWorldState::OnGlobals>(this);
    m_questsConnection = aDispatcher.sink<NotifyQuests>().connect<&GuestWorldState::OnQuests>(this);
    m_scriptResultsConnection = aDispatcher.sink<NotifyScriptResults>().connect<&GuestWorldState::OnScriptResults>(this);
    // this is what stops guests (clients connected to the host) from starting their own quests or running
    // their own world scripts: the host's game decides
    m_engine.SetDecidesWorld(false);
}

GuestWorldState::~GuestWorldState() noexcept
{
    m_engine.SetDecidesWorld(true);
}

void GuestWorldState::OnGlobals(const NotifyGlobals& acMessage) noexcept
{
    size_t missing = 0;
    for (const auto& cValue : acMessage.Values)
        if (!m_engine.SetGlobal(cValue))
            ++missing;
    if (missing)
        spdlog::warn("{}: {} of the host's {} globals are not in this game (or constant here)", __FUNCTION__, missing,
                     acMessage.Values.size());
}

void GuestWorldState::OnQuests(const NotifyQuests& acMessage) noexcept
{
    for (const auto& cQuest : acMessage.Quests)
        m_engine.ApplyQuest(cQuest, acMessage.Join);
}

void GuestWorldState::OnScriptResults(const NotifyScriptResults& acMessage) noexcept
{
    for (const auto& cResult : acMessage.Results)
        if (!m_engine.ApplyScriptResult(cResult))
            spdlog::debug("{}: object {:X} of a host script's result is not in this game", __FUNCTION__, cResult.Object.LogFormat());
}
