#pragma once

/**
 * @brief Dispatched whenever the lock status on an object changes.
 */
struct LockChangeEvent
{
    LockChangeEvent(uint32_t aFormId, bool aIsLocked, uint8_t aLockLevel, bool aByPlayer)
        : FormId(aFormId)
        , IsLocked(aIsLocked)
        , LockLevel(aLockLevel)
        , ByPlayer(aByPlayer)
    {
    }

    uint32_t FormId;
    bool IsLocked;
    uint8_t LockLevel;
    // The player picked it or used a key (ScopedPlayerLockOverride); else the world did (a script, a cell load).
    bool ByPlayer;
};
