#pragma once

#include <animation/LocalAnimationComponent.h>
#include <animation/RemoteAnimationComponent.h>
#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>

struct World;
struct ReferencesMoveRequest;

/**
 * @brief Plays a copy's owner's actions on it: what to take, wait for, skip or stage (IAnimationBody does the acts).
 */
struct AnimationSystem
{
    /**
     * @brief Ran periodically to check for new animations to apply.
     * @param aWorld The registry where the actor in question lives.
     * @param aEntity The entity attached to the actor.
     * @param aFormId The copy's actor.
     * @param aAnimationComponent The animation component attached to the actor.
     * @param aTick The current tick.
     */
    static void Update(World& aWorld, entt::entity aEntity, uint32_t aFormId, RemoteAnimationComponent& aAnimationComponent, uint64_t aTick) noexcept;
    /**
     * @brief Sets up the animation system for a particular actor.
     * @param aWorld The registry where the actor in question lives.
     * @param aEntity The entity attached to the actor.
     */
    static void Setup(World& aWorld, entt::entity aEntity) noexcept;
    /**
     * @brief Unregisters an actor from receiving remote animations.
     *
     * This function is not being used.
     *
     * @param aWorld The registry where the actor in question lives.
     * @param aEntity The entity attached to the actor.
     */
    static void Clean(World& aWorld, entt::entity aEntity) noexcept;
    /**
     * @brief Adds an action (animation) to be processed.
     * @param aAnimationComponent The animation component attached to the actor in question.
     * @param acActionDiff The differential data of the animation.
     */
    static void AddAction(RemoteAnimationComponent& aAnimationComponent, const std::string& acActionDiff) noexcept;
    /**
     * @brief Serializes the actions to-be-sent.
     * @param aWorld The registry where the actor in question lives.
     * @param aMovementSnapshot The output of the animation data.
     * @param localComponent The local component of the actor whose data is to be serialized.
     * @param animationComponent The local animation component of the actor, used to give the output the session id of the actor.
     * @param formIdComponent The form id component of the actor, used to fetch the actor pointer.
     */
    static void Serialize(World& aWorld, ReferencesMoveRequest& aMovementSnapshot, LocalComponent& localComponent, LocalAnimationComponent& animationComponent, FormIdComponent& formIdComponent);
    // An action's target between games: an actor by its session id (ActionEvent::TargetActor), any other reference
    // by its form id (the same in both games; a temporary one names nothing there and is dropped). Game thread.
    static void TargetToNetwork(ActionEvent& aAction) noexcept;
    static void TargetFromNetwork(ActionEvent& aAction) noexcept;
    /**
     * @brief Serializes the actions to-be-sent.
     *
     * This function is not being used.
     */
    static bool Serialize(World& aWorld, const ActionEvent& aActionEvent, const ActionEvent& aLastProcessedAction, std::string* apData);
};
