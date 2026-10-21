#include <Services/Guest/GuestWorldState.h>

#include <Bridges/GuestEngine.h>
#include <Messages/WorldState.h>

GuestWorldState::GuestWorldState(entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_engine(aEngine)
{
    m_globalsConnection = aDispatcher.sink<NotifyGlobals>().connect<&GuestWorldState::OnGlobals>(this);
    m_questsConnection = aDispatcher.sink<NotifyQuests>().connect<&GuestWorldState::OnQuests>(this);
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
