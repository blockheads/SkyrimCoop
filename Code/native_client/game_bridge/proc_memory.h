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
    std::string name;  // e.g., "SkyrimSE.exe", "[heap]", etc.
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

    bool IsOpen() const { return m_fd != -1; }
    pid_t GetPid() const { return m_pid; }

    // Parse /proc/pid/maps and cache readable regions
    bool LoadMemoryMaps();
    // Check if addr falls within a readable region; returns region name or empty
    std::string ValidateAddress(uint64_t addr) const;
    // Log summary of mapped regions (call once after Open)
    void DumpMemoryMapSummary() const;
    // Get count of loaded regions
    size_t GetRegionCount() const { return m_regions.size(); }

private:
    int m_fd = -1;
    pid_t m_pid = 0;
    bool m_isTracer = false;
    std::vector<MemoryRegion> m_regions;
};
