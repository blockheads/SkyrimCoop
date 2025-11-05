#pragma once

#include <entt/entt.hpp>
#include <chrono>

struct RunnerService;
struct ModSystem;

/**
 * @brief Polymorphic base class for both client World and Server::World.
 *
 * This allows us to swap between client and host modes at runtime.
 * When F9 is pressed to start hosting, we destroy the client World
 * and create a Server::World in its place.
 *
 * Both World types inherit from this base and can be accessed via
 * the singleton pattern using WorldBase::Get().
 */
struct WorldBase : entt::registry
{
    virtual ~WorldBase() = default;

    // Pure virtual methods that both worlds must implement
    virtual void Update() noexcept = 0;
    virtual bool IsHost() const noexcept = 0;

    virtual entt::dispatcher& GetDispatcher() noexcept = 0;
    virtual RunnerService& GetRunner() noexcept = 0;
    virtual ModSystem& GetModSystem() noexcept = 0;

    // Singleton pattern - returns the active world (client or server)
    static WorldBase& Get() noexcept;
    static void Set(WorldBase* apWorld) noexcept;
    static void Destroy() noexcept;

protected:
    WorldBase() = default;

private:
    static inline WorldBase* s_pInstance = nullptr;
};
