#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <optional>

// The session ids of this game's actors (their LocalComponent's or RemoteComponent's), with no engine.
namespace Utils
{
std::optional<uint32_t> GetSessionId(entt::entity aEntity) noexcept;
// The local form id of the actor with this session id; 0 if none.
uint32_t GetFormIdBySessionId(uint32_t aSessionId) noexcept;
// The session id of the actor with this local form id, if the mod tracks it.
std::optional<uint32_t> GetSessionIdByFormId(uint32_t aFormId) noexcept;
} // namespace Utils
