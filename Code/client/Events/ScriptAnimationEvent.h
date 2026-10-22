#pragma once

/**
 * @brief Dispatched when an object animation has been triggered.
 */
struct ScriptAnimationEvent
{
    ScriptAnimationEvent(uint32_t aFormID, std::string aAnimation, std::string aEventName)
        : FormID(aFormID)
        , Animation(aAnimation)
        , EventName(aEventName)
    {
    }

    uint32_t FormID;
    std::string Animation;
    std::string EventName;
};
