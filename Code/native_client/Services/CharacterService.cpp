// Server PCH prerequisites for encoding headers (must come first)
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>

#include <glm/glm.hpp>

using TiltedPhoques::Map;

#include "CharacterService.h"

#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"
#include "../transport/LoopbackTransport.h"
#include <nlohmann/json.hpp>
#include "json_protocol.h"

#include <Client.hpp>
#include <Packet.hpp>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Structs/ReferenceUpdate.h>

// Helper: compute squared distance for change detection
static float DistanceSquared(const glm::vec3& a, const glm::vec3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// Threshold for considering a position "changed" (1 unit squared)
static constexpr float kPositionChangeThresholdSq = 1.0f;
// Threshold for rotation change (radians)
static constexpr float kRotationChangeThreshold = 0.01f;

CharacterService::CharacterService(GameReader& aReader, TcpClient& aClient, entt::registry& aRegistry)
    : m_reader(aReader)
    , m_client(aClient)
    , m_registry(aRegistry)
{
    spdlog::info("CharacterService initialized");
}

// --- Hook event dispatch (JSON wire protocol) ---

void CharacterService::OnHookEvent(const nlohmann::json& aMsg)
{
    const auto& op = aMsg[JsonField::kOp].get_ref<const std::string&>();

    if (op == JsonOp::kActorAdded) {
        HandleActorAdded(aMsg.value(JsonField::kPtr, 0ull),
                         aMsg.value(JsonField::kFormId, 0u));
    } else if (op == JsonOp::kActorRemoved) {
        HandleActorRemoved(aMsg.value(JsonField::kPtr, 0ull),
                           aMsg.value(JsonField::kFormId, 0u));
    } else if (op == JsonOp::kSetPosition) {
        uint32_t formId = aMsg.value(JsonField::kFormId, 0u);
        if (formId != 0) {
            float px = aMsg.value(JsonField::kX, 0.0f);
            float py = aMsg.value(JsonField::kY, 0.0f);
            float pz = aMsg.value(JsonField::kZ, 0.0f);
            float rx = aMsg.value(JsonField::kRx, 0.0f);
            float rz = aMsg.value(JsonField::kRz, 0.0f);
            m_positionCache[formId] = {glm::vec3(px, py, pz), glm::vec2(rx, rz)};
        }
    } else if (op == JsonOp::kCharacterCtor) {
        HandleCharacterCtor(aMsg.value(JsonField::kPtr, 0ull),
                            aMsg.value(JsonField::kFormId, 0u));
    } else if (op == JsonOp::kSpawnActor) {
        HandleSpawnActor(aMsg.value(JsonField::kPtr, 0ull),
                         aMsg.value(JsonField::kFormId, 0u));
    } else if (op == JsonOp::kDeathItems) {
        HandleDeathItems(aMsg.value(JsonField::kPtr, 0ull),
                         aMsg.value(JsonField::kFormId, 0u));
    } else if (op == JsonOp::kPerformAction) {
        HandlePerformAction(aMsg.value(JsonField::kActorFormId, 0u),
                            aMsg.value(JsonField::kActionFormId, 0u));
    } else if (op == JsonOp::kActorProcess) {
        // Resolve any deferred actors — actor_process fires every tick with valid formId+ptr
        TryResolvePending(aMsg.value(JsonField::kPtr, 0ull),
                          aMsg.value(JsonField::kFormId, 0u));
    } else if (op == JsonOp::kPlayAnimation) {
        // Animation event -- logged for diagnostics, no action needed here
        spdlog::debug("CharacterService: play_animation formId={:#x}",
                      aMsg.value(JsonField::kThisFormId, 0u));
    } else if (op == JsonOp::kMountPackage) {
        // Mount package event -- logged for diagnostics
        spdlog::debug("CharacterService: mount_package actorFormId={:#x} mountFormId={:#x}",
                      aMsg.value(JsonField::kActorFormId, 0u),
                      aMsg.value(JsonField::kMountFormId, 0u));
    } else if (op == JsonOp::kSpawnResult) {
        OnSpawnResult(aMsg.value(JsonField::kFormId, 0u),
                      aMsg.value(JsonField::kPtr, uint64_t(0)),
                      aMsg.value(JsonField::kSuccess, false));
    }
}

// --- Actor lifecycle ---

void CharacterService::HandleActorAdded(uint64_t aActorPtr, uint32_t aFormId)
{
    if (aActorPtr == 0) return;

    uint32_t formId = aFormId;
    if (formId == 0) {
        // Defer registration: stash pointer, resolve when a later event provides formId
        m_pendingPtrs.insert(aActorPtr);
        spdlog::debug("CharacterService: Deferred actor at ptr={:#x} (formId=0, pending={})",
                       aActorPtr, m_pendingPtrs.size());
        return;
    }

    // If this pointer was pending, it's now resolved
    m_pendingPtrs.erase(aActorPtr);

    // Register in pointer table
    m_reader.GetPointers().Insert(formId, aActorPtr);

    // Check if this is the local player
    if (formId == kPlayerFormId) {
        m_localPlayerPtr = aActorPtr;
        m_localPlayerFormId = formId;
        spdlog::info("CharacterService: Local player registered formId={:#x} ptr={:#x}", formId, aActorPtr);
    }

    // Check if an entity already exists for this formId
    bool found = false;
    auto localView = m_registry.view<LocalComponent>();
    for (auto entity : localView) {
        auto& comp = localView.get<LocalComponent>(entity);
        if (comp.formId == formId) {
            comp.gamePtr = aActorPtr;
            found = true;
            break;
        }
    }

    if (!found) {
        auto remoteView = m_registry.view<RemoteComponent>();
        for (auto entity : remoteView) {
            auto& comp = remoteView.get<RemoteComponent>(entity);
            if (comp.formId == formId) {
                comp.gamePtr = aActorPtr;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        // New local actor -- create ECS entity with LocalComponent
        auto entity = m_registry.create();
        m_registry.emplace<LocalComponent>(entity, formId, aActorPtr, false, false);
        spdlog::info("CharacterService: Created local entity for formId={:#x}", formId);
    }
}

void CharacterService::HandleCharacterCtor(uint64_t aActorPtr, uint32_t aFormId)
{
    // Character constructor hook -- treat same as ActorAdded for tracking
    HandleActorAdded(aActorPtr, aFormId);
}

void CharacterService::TryResolvePending(uint64_t aActorPtr, uint32_t aFormId)
{
    if (aFormId == 0 || aActorPtr == 0) return;
    if (m_pendingPtrs.empty()) return;

    auto it = m_pendingPtrs.find(aActorPtr);
    if (it == m_pendingPtrs.end()) return;

    spdlog::info("CharacterService: Resolved deferred actor ptr={:#x} -> formId={:#x}", aActorPtr, aFormId);
    m_pendingPtrs.erase(it);
    HandleActorAdded(aActorPtr, aFormId);
}

void CharacterService::HandleSpawnActor(uint64_t aActorPtr, uint32_t aFormId)
{
    // Spawn actor hook -- treat same as ActorAdded for tracking
    HandleActorAdded(aActorPtr, aFormId);
}

void CharacterService::HandleActorRemoved(uint64_t aActorPtr, uint32_t aFormId)
{
    if (aActorPtr == 0) return;

    uint32_t formId = aFormId;
    if (formId == 0) {
        // Fallback: try to find entity by pointer
        auto localView = m_registry.view<LocalComponent>();
        for (auto entity : localView) {
            auto& comp = localView.get<LocalComponent>(entity);
            if (comp.gamePtr == aActorPtr) {
                spdlog::info("CharacterService: Removed local entity by ptr={:#x}", aActorPtr);
                m_registry.destroy(entity);
                return;
            }
        }
        auto remoteView = m_registry.view<RemoteComponent>();
        for (auto entity : remoteView) {
            auto& comp = remoteView.get<RemoteComponent>(entity);
            if (comp.gamePtr == aActorPtr) {
                spdlog::info("CharacterService: Removed remote entity by ptr={:#x}", aActorPtr);
                m_registry.destroy(entity);
                return;
            }
        }
        spdlog::warn("CharacterService: ActorRemoved could not identify actor at ptr={:#x}", aActorPtr);
        return;
    }

    // Remove from pointer table and position cache
    m_reader.GetPointers().Remove(formId);
    m_positionCache.erase(formId);

    // Clear local player tracking if this was the player
    if (formId == m_localPlayerFormId) {
        m_localPlayerPtr = 0;
        m_localPlayerFormId = 0;
        spdlog::info("CharacterService: Local player removed");
    }

    // Find and destroy the ECS entity
    auto localView = m_registry.view<LocalComponent>();
    for (auto entity : localView) {
        auto& comp = localView.get<LocalComponent>(entity);
        if (comp.formId == formId) {
            m_registry.destroy(entity);
            spdlog::info("CharacterService: Removed local entity formId={:#x}", formId);
            return;
        }
    }

    auto remoteView = m_registry.view<RemoteComponent>();
    for (auto entity : remoteView) {
        auto& comp = remoteView.get<RemoteComponent>(entity);
        if (comp.formId == formId) {
            m_registry.destroy(entity);
            spdlog::info("CharacterService: Removed remote entity formId={:#x}", formId);
            return;
        }
    }
}

void CharacterService::HandleDeathItems(uint64_t aActorPtr, uint32_t aFormId)
{
    // Death items hook indicates actor is dying -- mark as dead in ECS
    if (aActorPtr == 0) return;

    uint32_t formId = aFormId;
    if (formId == 0) return;

    auto view = m_registry.view<LocalComponent>();
    for (auto entity : view) {
        auto& comp = view.get<LocalComponent>(entity);
        if (comp.formId == formId) {
            comp.isDead = true;
            spdlog::info("CharacterService: Actor died formId={:#x}", formId);
            return;
        }
    }
}

// --- Action / animation handling ---

void CharacterService::HandlePerformAction(uint32_t aActorFormId, uint32_t aActionFormId)
{
    if (aActorFormId == 0) return;

    // Find the entity and update animation component
    auto view = m_registry.view<LocalComponent>();
    for (auto entity : view) {
        auto& comp = view.get<LocalComponent>(entity);
        if (comp.formId != aActorFormId) continue;

        // Ensure entity has an AnimationComponent
        if (!m_registry.all_of<AnimationComponent>(entity)) {
            m_registry.emplace<AnimationComponent>(entity);
        }

        auto& animComp = m_registry.get<AnimationComponent>(entity);
        animComp.lastActionId = aActionFormId;
        animComp.lastTargetFormId = 0; // TODO(09.1): move action type read to DLL
        animComp.dirty = true;

        spdlog::debug("CharacterService: PerformAction formId={:#x} actionFormId={:#x}",
                       aActorFormId, aActionFormId);
        return;
    }
}

// --- Periodic updates ---

void CharacterService::Update(float aDeltaTime)
{
    m_moveSyncTimer += aDeltaTime;
    if (m_moveSyncTimer >= kMoveSyncInterval) {
        m_moveSyncTimer = 0.0f;
        SyncMovement();
    }

    // Apply interpolated positions to remote actors via DLL commands
    ApplyRemotePositions();
}

void CharacterService::SyncMovement()
{
    // Read positions for all local actors and detect changes
    auto view = m_registry.view<LocalComponent>();

    for (auto entity : view) {
        auto& comp = view.get<LocalComponent>(entity);
        if (comp.gamePtr == 0) continue;

        // Read current position and rotation from cache (populated by set_position events)
        // No /proc/mem fallback -- under Wine/Proton, pread on game memory does not work.
        // If no set_position has fired for this actor yet, skip until one arrives.
        auto cacheIt = m_positionCache.find(comp.formId);
        if (cacheIt == m_positionCache.end()) {
            continue; // No cached data yet -- wait for DLL to send a set_position event
        }

        glm::vec3 currentPos = cacheIt->second.position;
        glm::vec2 currentRot = cacheIt->second.rotation;

        // Check if position changed since last sync
        bool changed = false;

        // Find cached entry for this formId
        CachedPosition* pCached = nullptr;
        for (auto& entry : m_cachedLocalPositions) {
            if (entry.first == comp.formId) {
                pCached = &entry.second;
                break;
            }
        }

        if (pCached == nullptr) {
            // First time seeing this actor -- always send
            m_cachedLocalPositions.push_back({comp.formId, {currentPos, currentRot}});
            changed = true;
        } else {
            // Check for significant change
            if (DistanceSquared(currentPos, pCached->position) > kPositionChangeThresholdSq ||
                std::fabs(currentRot.x - pCached->rotation.x) > kRotationChangeThreshold ||
                std::fabs(currentRot.y - pCached->rotation.y) > kRotationChangeThreshold) {
                pCached->position = currentPos;
                pCached->rotation = currentRot;
                changed = true;
            }
        }

        if (changed && m_pLoopback) {
            ClientReferencesMoveRequest moveReq;
            moveReq.Tick = 0; // Phase 8 MVP: tick ordering deferred

            ReferenceUpdate update;
            update.UpdatedMovement.Position.x = currentPos.x;
            update.UpdatedMovement.Position.y = currentPos.y;
            update.UpdatedMovement.Position.z = currentPos.z;
            update.UpdatedMovement.Rotation.x = currentRot.x;
            update.UpdatedMovement.Rotation.y = currentRot.y;
            moveReq.Updates[comp.formId] = update;

            m_pLoopback->SendToServer(moveReq);
            spdlog::debug("CharacterService: Sent movement via loopback formId={:#x}", comp.formId);
        } else if (changed && m_pJoinClient) {
            ClientReferencesMoveRequest moveReq;
            moveReq.Tick = 0;

            ReferenceUpdate update;
            update.UpdatedMovement.Position.x = currentPos.x;
            update.UpdatedMovement.Position.y = currentPos.y;
            update.UpdatedMovement.Position.z = currentPos.z;
            update.UpdatedMovement.Rotation.x = currentRot.x;
            update.UpdatedMovement.Rotation.y = currentRot.y;
            moveReq.Updates[comp.formId] = update;

            TiltedPhoques::Buffer buffer(1 << 16);
            TiltedPhoques::Buffer::Writer writer(&buffer);
            writer.WriteBits(0, 8); // packet type prefix byte
            moveReq.Serialize(writer);
            TiltedPhoques::PacketView packet(
                reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());
            m_pJoinClient->Send(&packet);
            spdlog::debug("CharacterService: Sent movement via JoinClient formId={:#x}", comp.formId);
        }
    }

    // Sync animation variables for local actors with dirty animation state
    auto animView = m_registry.view<LocalComponent, AnimationComponent>();
    for (auto entity : animView) {
        auto& animComp = animView.get<AnimationComponent>(entity);
        if (!animComp.dirty) continue;

        auto& localComp = animView.get<LocalComponent>(entity);
        animComp.dirty = false;

        if (m_pLoopback) {
            // TODO(Phase 8+): Build ClientAnimationRequest and send via loopback
            // Animation message construction is more complex (needs animation variable encoding).
            // Position sync is the Phase 8 MVP requirement.
            spdlog::debug("CharacterService: AnimVar sync formId={:#x} (loopback send deferred)",
                          localComp.formId);
        }
    }
}

void CharacterService::ApplyRemotePositions()
{
    // For each remote actor with an interpolation component,
    // send cmd_set_position to the DLL with the current interpolated position
    auto view = m_registry.view<RemoteComponent, InterpolationComponent>();

    for (auto entity : view) {
        auto& remote = view.get<RemoteComponent>(entity);
        auto& interp = view.get<InterpolationComponent>(entity);

        // Phase 15: Fallback to serverId map if gamePtr not yet populated
        if (remote.gamePtr == 0) {
            auto it = m_serverIdToPtr.find(remote.serverId);
            if (it != m_serverIdToPtr.end()) {
                remote.gamePtr = it->second;
            }
        }

        if (remote.gamePtr == 0 || !interp.initialized) continue;

        // Send the interpolated position to DLL for application
        SendSetPosition(remote.gamePtr, interp.currentPos, interp.currentRot);
    }
}

// --- Network message handlers ---

void CharacterService::OnAssignCharacter(uint32_t aServerId, uint32_t aFormId,
                                          const glm::vec3& aPosition, const glm::vec2& aRotation)
{
    // Server assigned a remote character to track
    // Create ECS entity with RemoteComponent + InterpolationComponent

    // Check if we already track this serverId
    auto view = m_registry.view<RemoteComponent>();
    for (auto entity : view) {
        auto& comp = view.get<RemoteComponent>(entity);
        if (comp.serverId == aServerId) {
            // Already exists -- update
            comp.formId = aFormId;
            comp.gamePtr = m_reader.LookupPointer(aFormId);

            if (m_registry.all_of<InterpolationComponent>(entity)) {
                auto& interp = m_registry.get<InterpolationComponent>(entity);
                interp.targetPos = aPosition;
                interp.currentPos = aPosition;
                interp.targetRot = aRotation;
                interp.currentRot = aRotation;
                interp.initialized = true;
            }

            spdlog::info("CharacterService: Updated remote character serverId={} formId={:#x}", aServerId, aFormId);
            return;
        }
    }

    // Create new remote entity
    auto entity = m_registry.create();
    uint64_t gamePtr = m_reader.LookupPointer(aFormId);

    m_registry.emplace<RemoteComponent>(entity, aFormId, gamePtr, aServerId);

    InterpolationComponent interp{};
    interp.targetPos = aPosition;
    interp.currentPos = aPosition;
    interp.targetRot = aRotation;
    interp.currentRot = aRotation;
    interp.initialized = true;
    m_registry.emplace<InterpolationComponent>(entity, interp);

    m_registry.emplace<AnimationComponent>(entity);

    // Send initial position to DLL if we have a pointer
    if (gamePtr != 0) {
        SendSetPosition(gamePtr, aPosition, aRotation);
    }

    // Phase 15: Send spawn command to DLL to create the actor in Skyrim
    SendSpawnActor(aFormId, aPosition, aRotation);

    spdlog::info("CharacterService: Assigned remote character serverId={} formId={:#x} pos=({:.0f},{:.0f},{:.0f})",
                 aServerId, aFormId, aPosition.x, aPosition.y, aPosition.z);
}

void CharacterService::OnRemoveCharacter(uint32_t aServerId)
{
    auto view = m_registry.view<RemoteComponent>();
    for (auto entity : view) {
        auto& comp = view.get<RemoteComponent>(entity);
        if (comp.serverId == aServerId) {
            spdlog::info("CharacterService: Removing remote character serverId={} formId={:#x}",
                         aServerId, comp.formId);

            // Phase 15: Send remove command to DLL
            uint64_t actorPtr = comp.gamePtr;
            if (actorPtr == 0) {
                auto it = m_serverIdToPtr.find(aServerId);
                if (it != m_serverIdToPtr.end()) {
                    actorPtr = it->second;
                }
            }
            SendRemoveActor(actorPtr);
            m_serverIdToPtr.erase(aServerId);

            m_registry.destroy(entity);
            return;
        }
    }

    spdlog::warn("CharacterService: OnRemoveCharacter could not find serverId={}", aServerId);
}

void CharacterService::OnReferencesMoveRequest(uint32_t aServerId,
                                                 const glm::vec3& aPosition,
                                                 const glm::vec2& aRotation)
{
    // Update interpolation target for a remote actor
    auto view = m_registry.view<RemoteComponent, InterpolationComponent>();

    for (auto entity : view) {
        auto& remote = view.get<RemoteComponent>(entity);
        if (remote.serverId != aServerId) continue;

        auto& interp = view.get<InterpolationComponent>(entity);
        interp.targetPos = aPosition;
        interp.targetRot = aRotation;

        if (!interp.initialized) {
            interp.currentPos = aPosition;
            interp.currentRot = aRotation;
            interp.initialized = true;
        }

        return;
    }

    spdlog::debug("CharacterService: Move request for unknown serverId={}", aServerId);
}

void CharacterService::OnRemoteAnimationUpdate(uint32_t aServerId, uint32_t aActionId, uint32_t aTargetFormId)
{
    auto view = m_registry.view<RemoteComponent, AnimationComponent>();

    for (auto entity : view) {
        auto& remote = view.get<RemoteComponent>(entity);
        if (remote.serverId != aServerId) continue;

        auto& anim = view.get<AnimationComponent>(entity);
        anim.lastActionId = aActionId;
        anim.lastTargetFormId = aTargetFormId;
        anim.dirty = true;

        // Send animation command to DLL immediately
        if (remote.gamePtr != 0) {
            SendPlayAnimation(remote.gamePtr, aActionId, aTargetFormId);
        }

        return;
    }
}

// --- DLL command senders (JSON wire protocol) ---

void CharacterService::SendSetPosition(uint64_t aActorPtr, const glm::vec3& aPos, const glm::vec2& aRot)
{
    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdSetPosition},
        {JsonField::kActorPtr, aActorPtr},
        {JsonField::kX, aPos.x},
        {JsonField::kY, aPos.y},
        {JsonField::kZ, aPos.z},
        {JsonField::kRx, aRot.x},
        {JsonField::kRz, aRot.y}
    };
    m_client.SendJsonLine(cmd);
}

void CharacterService::SendPlayAnimation(uint64_t aActorPtr, uint32_t aActionId, uint32_t aTargetFormId)
{
    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdPlayAnimation},
        {JsonField::kActorPtr, aActorPtr},
        {JsonField::kActionId, aActionId},
        {JsonField::kTargetFormId, aTargetFormId}
    };
    m_client.SendJsonLine(cmd);
}

// --- Phase 15: Spawn/remove command senders ---

void CharacterService::SendSpawnActor(uint32_t aFormId, const glm::vec3& aPosition, const glm::vec2& aRotation)
{
    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdSpawnActor},
        {JsonField::kFormId, aFormId},
        {JsonField::kX, aPosition.x},
        {JsonField::kY, aPosition.y},
        {JsonField::kZ, aPosition.z},
        {JsonField::kRx, aRotation.x},
        {JsonField::kRy, aRotation.y}
    };
    m_client.SendJsonLine(cmd);
    spdlog::info("CharacterService: Sent cmd_spawn_actor formId={:#x} pos=({:.0f},{:.0f},{:.0f})",
                 aFormId, aPosition.x, aPosition.y, aPosition.z);
}

void CharacterService::SendRemoveActor(uint64_t aActorPtr)
{
    if (aActorPtr == 0) return;

    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdRemoveActor},
        {JsonField::kActorPtr, aActorPtr}
    };
    m_client.SendJsonLine(cmd);
    spdlog::info("CharacterService: Sent cmd_remove_actor ptr={:#x}", aActorPtr);
}

void CharacterService::OnSpawnResult(uint32_t aFormId, uint64_t aActorPtr, bool aSuccess)
{
    if (!aSuccess || aActorPtr == 0) {
        spdlog::warn("CharacterService: Spawn failed for formId={:#x}", aFormId);
        return;
    }

    // Find the RemoteComponent with matching formId and no gamePtr yet
    auto view = m_registry.view<RemoteComponent>();
    for (auto entity : view) {
        auto& comp = view.get<RemoteComponent>(entity);
        if (comp.formId == aFormId && comp.gamePtr == 0) {
            comp.gamePtr = aActorPtr;
            m_serverIdToPtr[comp.serverId] = aActorPtr;
            spdlog::info("CharacterService: Spawn result received formId={:#x} ptr={:#x} serverId={}",
                         aFormId, aActorPtr, comp.serverId);

            // Send initial position if interpolation data is available
            if (m_registry.all_of<InterpolationComponent>(entity)) {
                auto& interp = m_registry.get<InterpolationComponent>(entity);
                if (interp.initialized) {
                    SendSetPosition(aActorPtr, interp.currentPos, interp.currentRot);
                }
            }
            return;
        }
    }

    spdlog::warn("CharacterService: Spawn result for formId={:#x} but no matching RemoteComponent", aFormId);
}
