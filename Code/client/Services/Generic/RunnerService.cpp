#include <Services/RunnerService.h>

#include <Events/UpdateEvent.h>
#include <Diagnostics/Profile.h>

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
