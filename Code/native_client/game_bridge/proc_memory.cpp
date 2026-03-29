#include "proc_memory.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
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
    m_regions.clear();
    m_pid = 0;
}

bool ProcMemReader::LoadMemoryMaps() {
    m_regions.clear();
    std::string path = "/proc/" + std::to_string(m_pid) + "/maps";
    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::error("Failed to open {}", path);
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        MemoryRegion region{};
        // Format: start-end perms offset dev inode pathname
        uint64_t start = 0, end = 0;
        char perms[5] = {};
        int parsed = sscanf(line.c_str(), "%lx-%lx %4s", &start, &end, perms);
        if (parsed < 3) continue;

        region.start = start;
        region.end = end;
        region.readable = (perms[0] == 'r');

        // Extract pathname (last field after inode)
        // Find the 6th field (pathname) by skipping 5 fields
        std::istringstream iss(line);
        std::string token;
        for (int i = 0; i < 5 && iss >> token; i++) {}
        if (iss >> token) {
            region.name = token;
        }

        m_regions.push_back(std::move(region));
    }

    spdlog::info("Loaded {} memory regions from {}", m_regions.size(), path);
    return true;
}

std::string ProcMemReader::ValidateAddress(uint64_t addr) const {
    for (const auto& r : m_regions) {
        if (addr >= r.start && addr < r.end && r.readable) {
            return r.name;
        }
    }
    return "";
}

uint64_t ProcMemReader::FindModuleBase(const char* aNameSuffix) const {
    std::string suffix(aNameSuffix);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
    for (const auto& r : m_regions) {
        if (!r.readable) continue;
        std::string nameLower = r.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower.size() >= suffix.size() &&
            nameLower.compare(nameLower.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return r.start;
        }
    }
    return 0;
}

ssize_t ProcMemReader::HexDump(uint64_t addr, size_t len, const char* aLabel) {
    if (len > 64) len = 64;
    uint8_t buf[64]{};
    ssize_t bytesRead = ::pread(m_fd, buf, len, static_cast<off_t>(addr));
    if (bytesRead <= 0) {
        spdlog::error("HexDump '{}' at {:#x}: pread failed: {} ({})",
            aLabel, addr, strerror(errno), errno);
        return bytesRead;
    }
    std::string hex;
    for (ssize_t i = 0; i < bytesRead; i++) {
        char tmp[4];
        snprintf(tmp, sizeof(tmp), "%02x ", buf[i]);
        hex += tmp;
    }
    spdlog::info("HexDump '{}' at {:#x} ({} bytes): {}", aLabel, addr, bytesRead, hex);
    return bytesRead;
}

void ProcMemReader::DumpMemoryMapSummary() const {
    spdlog::info("Memory map summary ({} regions):", m_regions.size());
    int count = 0;
    for (const auto& r : m_regions) {
        if (!r.name.empty() && r.readable) {
            spdlog::info("  {:#x}-{:#x} {} {}", r.start, r.end,
                r.readable ? "r" : "-", r.name);
            if (++count >= 50) {
                spdlog::info("  ... ({} more regions)", m_regions.size() - count);
                break;
            }
        }
    }
}
