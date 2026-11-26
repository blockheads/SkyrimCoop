#include <Services/Guest/GuestEye.h>

#include <Bridges/IGuestEngine.h>
#include <Messages/NotifyStealthEye.h>

GuestEye::GuestEye(entt::dispatcher& aDispatcher, IGuestEngine& aEngine) noexcept
    : m_engine(aEngine)
{
    Listen<NotifyStealthEye, &GuestEye::OnEye>(m_connections, aDispatcher, this);
}

GuestEye::~GuestEye() noexcept
{
    m_engine.ReleaseEye();
}

void GuestEye::OnEye(const NotifyStealthEye& acMessage) noexcept
{
    m_engine.HoldEye({.Level = acMessage.Level, .Seeing = acMessage.Seeing, .Stealth = acMessage.Stealth});
}
