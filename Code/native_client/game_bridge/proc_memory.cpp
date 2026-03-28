#include "proc_memory.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

// Check Yama ptrace_scope: 0 = classic, 1+ = restricted
static int GetPtraceScope() {
    std::ifstream f("/proc/sys/kernel/yama/ptrace_scope");
    int scope = 0;
    if (f.is_open()) {
        f >> scope;
    }
    return scope;
}

ProcMemReader::~ProcMemReader() {
    Close();
}

bool ProcMemReader::Open(pid_t pid) {
    if (m_fd != -1)
        Close();

    m_pid = pid;

    // If ptrace_scope >= 1, we need to establish a tracer relationship
    // via PTRACE_SEIZE (non-stopping attach) to read /proc/<pid>/mem.
    int scope = GetPtraceScope();
    if (scope >= 1 && pid != getpid()) {
        long ret = ptrace(PTRACE_SEIZE, pid, nullptr, nullptr);
        if (ret == -1) {
            spdlog::error("ptrace(PTRACE_SEIZE, {}) failed: {} ({})", pid, strerror(errno), errno);
            // Don't fail yet -- try opening the fd anyway
        } else {
            m_isTracer = true;
            spdlog::debug("ptrace SEIZE established for pid {}", pid);
        }
    }

    std::string path = "/proc/" + std::to_string(pid) + "/mem";
    m_fd = ::open(path.c_str(), O_RDONLY);
    if (m_fd == -1) {
        spdlog::error("Failed to open {} for reading: {} ({})", path, strerror(errno), errno);
        if (m_isTracer) {
            ptrace(PTRACE_DETACH, m_pid, nullptr, nullptr);
            m_isTracer = false;
        }
        m_pid = 0;
        return false;
    }

    spdlog::info("Opened /proc/{}/mem (fd={}, ptrace_scope={})", pid, m_fd, scope);
    return true;
}

ssize_t ProcMemReader::Read(void* buf, size_t len, uint64_t addr) {
    if (m_fd == -1)
        return -1;
    return ::pread(m_fd, buf, len, static_cast<off_t>(addr));
}

void ProcMemReader::Close() {
    if (m_fd != -1) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_isTracer && m_pid != 0) {
        // PTRACE_SEIZE leaves tracee running. PTRACE_DETACH requires the
        // tracee to be in ptrace-stop. Interrupt it first, then detach.
        if (ptrace(PTRACE_INTERRUPT, m_pid, nullptr, nullptr) == 0) {
            int status;
            if (waitpid(m_pid, &status, __WALL) > 0) {
                ptrace(PTRACE_DETACH, m_pid, nullptr, nullptr);
            }
        } else {
            // Fallback: try direct detach
            ptrace(PTRACE_DETACH, m_pid, nullptr, nullptr);
        }
        m_isTracer = false;
        spdlog::debug("ptrace detached from pid {}", m_pid);
    }
    m_pid = 0;
}
