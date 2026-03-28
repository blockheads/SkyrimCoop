#include <catch2/catch.hpp>
#include <vector>

#include "command_queue.h"

TEST_CASE("CommandQueue_SingleEnqueueDrain", "[CommandQueue]")
{
    CommandQueue queue;

    CommandSlot cmd{};
    cmd.opcode = CMD_SET_POSITION;
    cmd.argCount = 3;
    cmd.args[0] = 100;
    cmd.args[1] = 200;
    cmd.args[2] = 300;

    REQUIRE(queue.Enqueue(cmd));
    REQUIRE(queue.Size() == 1);

    uint32_t drained = 0;
    drained = queue.Drain([&](const CommandSlot& slot) {
        REQUIRE(slot.opcode == CMD_SET_POSITION);
        REQUIRE(slot.argCount == 3);
        REQUIRE(slot.args[0] == 100);
        REQUIRE(slot.args[1] == 200);
        REQUIRE(slot.args[2] == 300);
    });

    REQUIRE(drained == 1);
    REQUIRE(queue.Size() == 0);
}

TEST_CASE("CommandQueue_FullCapacity", "[CommandQueue]")
{
    CommandQueue queue;

    // Enqueue 256 commands (full capacity)
    for (uint32_t i = 0; i < CommandQueue::QUEUE_SIZE; i++) {
        CommandSlot cmd{};
        cmd.opcode = CMD_CAST_SPELL;
        cmd.argCount = 1;
        cmd.args[0] = i;
        REQUIRE(queue.Enqueue(cmd));
    }

    REQUIRE(queue.Size() == CommandQueue::QUEUE_SIZE);
    REQUIRE(queue.IsFull());

    // Drain all and verify FIFO order
    uint32_t expectedIndex = 0;
    uint32_t drained = queue.Drain([&](const CommandSlot& slot) {
        REQUIRE(slot.args[0] == expectedIndex);
        expectedIndex++;
    });

    REQUIRE(drained == CommandQueue::QUEUE_SIZE);
    REQUIRE(queue.Size() == 0);
}

TEST_CASE("CommandQueue_OverflowReturnsFalse", "[CommandQueue]")
{
    CommandQueue queue;

    // Fill to capacity
    for (uint32_t i = 0; i < CommandQueue::QUEUE_SIZE; i++) {
        CommandSlot cmd{};
        cmd.opcode = CMD_EQUIP_ITEM;
        cmd.argCount = 0;
        REQUIRE(queue.Enqueue(cmd));
    }

    // One more should fail
    CommandSlot overflow{};
    overflow.opcode = CMD_EQUIP_ITEM;
    overflow.argCount = 0;
    REQUIRE_FALSE(queue.Enqueue(overflow));
    REQUIRE(queue.IsFull());
}

TEST_CASE("CommandQueue_DrainEmpty", "[CommandQueue]")
{
    CommandQueue queue;

    uint32_t drained = queue.Drain([](const CommandSlot&) {
        FAIL("Should not be called on empty queue");
    });

    REQUIRE(drained == 0);
    REQUIRE(queue.Size() == 0);
}

TEST_CASE("CommandQueue_InterleavedOps", "[CommandQueue]")
{
    CommandQueue queue;

    // Enqueue 10
    for (uint32_t i = 0; i < 10; i++) {
        CommandSlot cmd{};
        cmd.opcode = CMD_ADD_ITEM;
        cmd.argCount = 1;
        cmd.args[0] = i;
        REQUIRE(queue.Enqueue(cmd));
    }

    REQUIRE(queue.Size() == 10);

    // Drain 5
    uint32_t expectedIndex = 0;
    uint32_t drainCount = 0;
    queue.Drain([&](const CommandSlot& slot) {
        if (drainCount < 5) {
            REQUIRE(slot.args[0] == expectedIndex);
            expectedIndex++;
        }
        drainCount++;
    });
    // Note: Drain drains ALL available, so it drained all 10
    // Re-approach: drain all, verify order for all 10

    // Reset for a clean interleaved test
    CommandQueue queue2;

    // Enqueue 10 items (indices 0-9)
    for (uint32_t i = 0; i < 10; i++) {
        CommandSlot cmd{};
        cmd.opcode = CMD_ADD_ITEM;
        cmd.argCount = 1;
        cmd.args[0] = i;
        REQUIRE(queue2.Enqueue(cmd));
    }

    // Drain all 10
    std::vector<uint64_t> results;
    queue2.Drain([&](const CommandSlot& slot) {
        results.push_back(slot.args[0]);
    });
    REQUIRE(results.size() == 10);

    // Enqueue 10 more (indices 10-19)
    for (uint32_t i = 10; i < 20; i++) {
        CommandSlot cmd{};
        cmd.opcode = CMD_ADD_ITEM;
        cmd.argCount = 1;
        cmd.args[0] = i;
        REQUIRE(queue2.Enqueue(cmd));
    }

    // Drain remaining 10
    queue2.Drain([&](const CommandSlot& slot) {
        results.push_back(slot.args[0]);
    });

    REQUIRE(results.size() == 20);
    for (uint32_t i = 0; i < 20; i++) {
        REQUIRE(results[i] == i);
    }
}
