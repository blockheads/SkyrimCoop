#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sys/types.h>

struct MemoryRegion {
    uint64_t start;
    uint64_t end;
    bool readable;
    std::string name;
};

class ProcMemReader {
public:
    ~ProcMemReader();

    // Open /proc/pid/mem with ptrace SEIZE for Yama bypass
    bool Open(pid_t pid);
    void Close();

    // Read `len` bytes from address `addr` into `buf`
    // Returns bytes read, or -1 on error
    ssize_t Read(void* buf, size_t len, uint64_t addr);

    // Typed read helper
    template<typename T>
    T ReadValue(uint64_t addr) {
        T val{};
        Read(&val, sizeof(T), addr);
        return val;
    }

    // Load /proc/pid/maps into m_regions for address validation
    bool LoadMemoryMaps();

    // Check if addr falls in a mapped region, return region name or ""
    std::string ValidateAddress(uint64_t addr) const;

    // Find the base address of a named module in /proc/pid/maps
    // Returns 0 if not found. Searches m_regions for the first readable region
    // whose name ends with the given suffix (case-insensitive).
    uint64_t FindModuleBase(const char* aNameSuffix) const;

    // Read and log a hex dump of `len` bytes at `addr` for diagnostics.
    // Logs one line of up to 64 bytes. Returns bytes successfully read.
    ssize_t HexDump(uint64_t addr, size_t len, const char* aLabel);

    // Log a summary of all mapped regions
    void DumpMemoryMapSummary() const;

    bool IsOpen() const { return m_fd != -1; }
    pid_t GetPid() const { return m_pid; }

private:
    int m_fd = -1;
    pid_t m_pid = 0;
    bool m_isTracer = false;
    std::vector<MemoryRegion> m_regions;
};
