#pragma once

#include <cstddef>
#include <cstdint>

struct _CONTEXT;

/**
 * @brief Who allocated and who freed the engine's memory, for crashes on freed memory (an object
 * still listed somewhere after it was deleted). The AddressSanitizer report for the engine's heap.
 *
 * Off unless SKYRIMCOOP_HEAP_HISTORY is set (to the events kept, in millions; default 2): it
 * records every MemoryManager allocation and free with its call stack, which costs frames. On a
 * crash, every register and stack value pointing into a recorded block gets the block's story:
 * allocated where, freed where (thread, ms before the crash), and what took its memory since.
 *
 * The ring only reaches back seconds. SKYRIMCOOP_HEAP_WATCH=0x215A0DB0,... keeps every allocation
 * and free of the blocks holding those addresses since boot, for an object allocated long before
 * the crash: heap addresses repeat from run to run when the game boots the same way, so a crash's
 * addresses can be watched in the next run (`coop_test.py repro --follow` does both runs).
 *
 * SKYRIMCOOP_HEAP_QUARANTINE=<MB> fills freed blocks with 0xDB and holds them back from reuse, so a
 * use after free faults at its first read (on 0xDBDB...) instead of later on whatever reused the
 * memory, with the freed block still in a register for the history to explain.
 *
 * Poison cannot catch a stale pointer that is only read and compared. SKYRIMCOOP_HEAP_GUARD=0x130
 * (block sizes, from a history report) gives blocks of those sizes pages of their own, made
 * inaccessible on free and never reused: the first access after the free faults, at the reader.
 * `coop_test.py repro --heap-history` turns the ring on; scripts/re/crash_report.py names the frames.
 */
namespace HeapHistory
{
// Whether any of it is on; sets it up on first call. The allocator hooks call the rest only then.
bool Start() noexcept;
void OnAllocate(const void* apBlock, size_t aSize) noexcept;
void OnFree(const void* apBlock) noexcept;
// With the quarantine on: takes the block (true; the caller must not free it) unless its size is
// unknown (allocated before our hooks).
bool Quarantine(void* apBlock, bool aAligned) noexcept;
// A block the quarantine let go of, for the caller to free now.
bool TakeEvicted(void*& apBlock, bool& aAligned) noexcept;
// With the guard on: a page of its own for a block of a guarded size (else null: the engine's).
void* GuardAllocate(size_t aSize) noexcept;
// A guarded block's size (0: not a live guarded block).
size_t GuardedSize(const void* apBlock) noexcept;
// Frees a guarded block (true), leaving its page inaccessible; false: not one.
bool GuardFree(void* apBlock) noexcept;
// Into the log: the story of every block the crash's registers and stack point into, and whether
// the faulting access (aFaultAddress) hit a freed guarded block.
void Dump(const _CONTEXT& acContext, uintptr_t aFaultAddress) noexcept;
} // namespace HeapHistory
