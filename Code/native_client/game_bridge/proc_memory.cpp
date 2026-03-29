#include "proc_memory.h"

#include <algorithm>
#include <cerrno>
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

    LoadMemoryMaps();
    DumpMemoryMapSummary();

    return true;
}

ssize_t ProcMemReader::Read(void* buf, size_t len, uint64_t addr) {
    if (m_fd == -1)
        return -1;

    std::string region = ValidateAddress(addr);
    if (region.empty()) {
        spdlog::warn("pread addr {:#x} not in any mapped region for pid {}", addr, m_pid);
    }

    ssize_t result = ::pread(m_fd, buf, len, static_cast<off_t>(addr));
    if (result <= 0) {
        if (region.empty()) {
            spdlog::error("pread failed for unmapped addr {:#x}: {} ({})", addr, strerror(errno), errno);
        } else {
            spdlog::error("pread failed for addr {:#x} in region '{}': {} ({})", addr, region, strerror(errno), errno);
        }
    }

    return result;
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

bool ProcMemReader::LoadMemoryMaps() {
    m_regions.clear();

    std::string path = "/proc/" + std::to_string(m_pid) + "/maps";
    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::error("Failed to open {} for memory map parsing", path);
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        // Format: START-END PERMS OFFSET DEV INODE PATHNAME
        // Example: 7f1234000-7f1235000 r-xp 00000000 08:01 12345 /path/to/file
        size_t dashPos = line.find('-');
        if (dashPos == std::string::npos) continue;

        size_t spacePos = line.find(' ', dashPos);
        if (spacePos == std::string::npos) continue;

        MemoryRegion region{};
        region.start = std::stoull(line.substr(0, dashPos), nullptr, 16);
        region.end = std::stoull(line.substr(dashPos + 1, spacePos - dashPos - 1), nullptr, 16);

        // Permissions field starts after the first space
        if (spacePos + 1 < line.size()) {
            region.readable = (line[spacePos + 1] == 'r');
        }

        // Extract pathname (skip perms, offset, dev, inode - 4 more space-delimited fields)
        size_t fieldStart = spacePos;
        for (int i = 0; i < 4 && fieldStart < line.size(); ++i) {
            fieldStart = line.find(' ', fieldStart + 1);
            if (fieldStart == std::string::npos) break;
        }
        if (fieldStart != std::string::npos && fieldStart + 1 < line.size()) {
            // Skip leading whitespace before pathname
            size_t nameStart = line.find_first_not_of(' ', fieldStart);
            if (nameStart != std::string::npos) {
                region.name = line.substr(nameStart);
            }
        }

        m_regions.push_back(std::move(region));
    }

    spdlog::info("Loaded {} memory regions from {}", m_regions.size(), path);

    // Log Skyrim-related regions
    for (const auto& r : m_regions) {
        std::string nameLower = r.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower.find("skyrimse") != std::string::npos || nameLower.find("skyrim") != std::string::npos) {
            spdlog::info("  Skyrim region: {:#x}-{:#x} {} '{}'",
                r.start, r.end, r.readable ? "r" : "-", r.name);
        }
    }

    return true;
}

std::string ProcMemReader::ValidateAddress(uint64_t addr) const {
    for (const auto& r : m_regions) {
        if (addr >= r.start && addr < r.end && r.readable) {
            return r.name;
        }
    }
    return {};
}

void ProcMemReader::DumpMemoryMapSummary() const {
    size_t readableCount = 0;
    for (const auto& r : m_regions) {
        if (r.readable) ++readableCount;
    }

    spdlog::info("Memory map summary for pid {}: {} total regions, {} readable",
        m_pid, m_regions.size(), readableCount);

    for (const auto& r : m_regions) {
        std::string nameLower = r.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower.find("skyrimse") != std::string::npos ||
            nameLower.find("skyrim_coop") != std::string::npos) {
            spdlog::info("  [{:#x}-{:#x}] {} '{}'",
                r.start, r.end, r.readable ? "readable" : "no-read", r.name);
        }
    }
}
