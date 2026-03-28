#pragma once
#include <cstdint>
#include <cstddef>
#include <sys/types.h>

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

private:
    int m_fd = -1;
    pid_t m_pid = 0;
    bool m_isTracer = false;
};
