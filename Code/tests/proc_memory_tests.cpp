#ifdef __linux__

#include <catch2/catch.hpp>
#include "game_bridge/proc_memory.h"
#include <unistd.h>

TEST_CASE("ProcMem_ReadOwnMemory", "[ProcMem]") {
    ProcMemReader reader;
    REQUIRE(reader.Open(getpid()));
    REQUIRE(reader.IsOpen());
    REQUIRE(reader.GetPid() == getpid());

    // Read a known stack variable
    volatile uint64_t canary = 0xDEADBEEFCAFEBABEULL;
    uint64_t readBack = 0;
    ssize_t bytesRead = reader.Read(&readBack, sizeof(readBack),
        reinterpret_cast<uint64_t>(&canary));

    REQUIRE(bytesRead == sizeof(readBack));
    REQUIRE(readBack == 0xDEADBEEFCAFEBABEULL);

    reader.Close();
}

TEST_CASE("ProcMem_ReadInvalidAddress", "[ProcMem]") {
    ProcMemReader reader;
    REQUIRE(reader.Open(getpid()));

    uint8_t buf[4];
    ssize_t bytesRead = reader.Read(buf, sizeof(buf), 0x1);
    REQUIRE(bytesRead == -1);

    reader.Close();
}

TEST_CASE("ProcMem_OpenClose", "[ProcMem]") {
    ProcMemReader reader;
    REQUIRE_FALSE(reader.IsOpen());
    REQUIRE(reader.GetPid() == 0);

    REQUIRE(reader.Open(getpid()));
    REQUIRE(reader.IsOpen());
    REQUIRE(reader.GetPid() == getpid());

    reader.Close();
    REQUIRE_FALSE(reader.IsOpen());
    REQUIRE(reader.GetPid() == 0);
}

TEST_CASE("ProcMem_ReadValue", "[ProcMem]") {
    ProcMemReader reader;
    REQUIRE(reader.Open(getpid()));

    volatile uint32_t testVal = 42;
    uint32_t result = reader.ReadValue<uint32_t>(
        reinterpret_cast<uint64_t>(&testVal));
    REQUIRE(result == 42);

    reader.Close();
}

TEST_CASE("ProcMem_ReadNotOpen", "[ProcMem]") {
    ProcMemReader reader;
    uint8_t buf[4];
    ssize_t bytesRead = reader.Read(buf, sizeof(buf), 0x1000);
    REQUIRE(bytesRead == -1);
}

#endif // __linux__
