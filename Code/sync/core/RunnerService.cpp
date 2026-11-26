#include <core/RunnerService.h>

#include <core/UpdateEvent.h>
#include <core/Profile.h>

RunnerService::RunnerService(entt::dispatcher& aDispatcher) noexcept
    : m_dispatcher(aDispatcher)
{
}

void RunnerService::Queue(std::function<void()> aFunctor) noexcept
{
    m_runner.Add(std::move(aFunctor));
}

void RunnerService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    PROFILE_ZONE("RunnerService::OnUpdate");
    m_runner.Drain();
}
