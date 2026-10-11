#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Lock-order checker for the engine's BSReadWriteLock (lockdep): every thread's held locks are
// tracked through hooks on the four lock functions. A deadlock is reported the first time its
// precondition is seen, whether or not this run's timing makes it hang:
// - inversion: one thread took A then B, another takes B while holding A, in conflicting modes
// - self-deadlock: a thread that holds a read lock asks for the write lock (waits forever)
// Disable with the environment variable SKYRIMCOOP_LOCKORDER=0.
namespace LockOrder
{
// Names a lock for reports (for example "actor 14 reference lock"); false when unknown.
using TDescribeLock = bool (*)(const void* apLock, char* apBuffer, size_t aSize);
void SetLockDescriber(TDescribeLock apDescribe) noexcept;

// Every thread's held locks with the code that took each one (watchdog thread, read-only peeks).
void LogHeldLocks() noexcept;

struct Stats
{
    bool Enabled;
    uint32_t Edges;
    uint32_t Reports;
    uint32_t TooDeep;   // acquisitions nested deeper than tracked (no order checks for them)
    uint32_t TableFull;   // lock pairs not recorded because the edge table is full
    uint32_t TableResets; // times the edge table filled up and started over
};
Stats GetStats() noexcept;
// The first reports, one line each.
std::vector<std::string> GetReports() noexcept;
} // namespace LockOrder
