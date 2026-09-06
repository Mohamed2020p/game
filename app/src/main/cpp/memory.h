// =============================================================================
//  memory.h — cross-process memory introspection for the game
// =============================================================================
//
//  Based on the educational version, but modified to read the TARGET game
//  process (com.miniclip.eightballpool or similar) using process_vm_readv.
//
//  All functions now work with a target PID. The PID is set by the overlay
//  after scanning /proc for the game's process.
// =============================================================================

#pragma once

#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <sys/uio.h>   // process_vm_readv
#include <unistd.h>    // getpid, read, close
#include <dirent.h>    // opendir, readdir

namespace mem {

// --- Global target PID (set from overlay) ---
extern pid_t target_pid;   // defined in main.cpp or overlay.cpp

// -----------------------------------------------------------------------------
// 1. The region record — one line of /proc/<pid>/maps
// -----------------------------------------------------------------------------
struct MemoryRegion {
    uint64_t start    = 0;
    uint64_t end      = 0;      // exclusive
    char     perms[5] = "----"; // "rwxp" style, NUL-terminated
    uint64_t offset   = 0;
    char     dev[12]  = "";
    uint64_t inode   = 0;
    std::string pathname;

    uint64_t Size()  const { return end - start; }
    bool IsReadable()const { return perms[0] == 'r'; }
    bool IsWritable()const { return perms[1] == 'w'; }
    bool IsPrivate() const { return perms[3] == 'p'; }
    bool IsAnonymous() const {
        return pathname.empty() ||
               pathname.rfind("[anon:", 0) == 0 ||
               pathname == "[heap]" ||
               pathname == "[stack]";
    }
    bool IsExecutable() const { return perms[2] == 'x'; }
};

inline bool ParseMapsLine(const char* line, MemoryRegion* out) {
    if (!line || !out) return false;
    *out = MemoryRegion{};
    unsigned long long start = 0, end = 0, off = 0, ino = 0;
    char perms[8] = {0};
    char dev[24]  = {0};
    int consumed = -1;
    const int fields = sscanf(line, "%llx-%llx %7s %llx %23s %llu %n",
                              &start, &end, perms, &off, dev, &ino, &consumed);
    if (fields < 6 || consumed <= 0) return false;
    out->start  = start;
    out->end    = end;
    out->offset = off;
    out->inode  = ino;
    std::strncpy(out->perms, perms, sizeof(out->perms) - 1);
    std::strncpy(out->dev, dev, sizeof(out->dev) - 1);
    std::string rest(line + consumed);
    while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r'))
        rest.pop_back();
    out->pathname = rest;
    return true;
}

// Read and parse the whole maps file of a process.
inline std::vector<MemoryRegion> ReadMaps(pid_t pid) {
    std::vector<MemoryRegion> regions;
    if (pid <= 0) return regions;
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    std::FILE* f = std::fopen(path, "r");
    if (!f) return regions;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        MemoryRegion r;
        if (ParseMapsLine(line, &r)) regions.push_back(std::move(r));
    }
    std::fclose(f);
    return regions;
}

// Find a process by name (e.g., "com.miniclip.eightballpool").
// Returns the first matching PID, or 0 if not found.
inline pid_t FindProcessByName(const char* name) {
    DIR* dir = opendir("/proc");
    if (!dir) return 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        // Only numeric directory names (PID)
        bool isNum = true;
        for (const char* p = entry->d_name; *p; ++p) {
            if (*p < '0' || *p > '9') { isNum = false; break; }
        }
        if (!isNum) continue;
        char path[256];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        std::FILE* f = std::fopen(path, "r");
        if (!f) continue;
        char cmdline[512];
        size_t n = fread(cmdline, 1, sizeof(cmdline)-1, f);
        fclose(f);
        if (n == 0) continue;
        cmdline[n] = '\0';
        // cmdline is null-terminated, but might have multiple strings.
        // Compare the first token (the executable path).
        if (strstr(cmdline, name) != nullptr) {
            pid_t pid = atoi(entry->d_name);
            closedir(dir);
            return pid;
        }
    }
    closedir(dir);
    return 0;
}

// -----------------------------------------------------------------------------
// 2. Region lookup helpers
// -----------------------------------------------------------------------------
inline const MemoryRegion* FindRegionForAddress(
        const std::vector<MemoryRegion>& regions, uint64_t addr) {
    for (const auto& r : regions)
        if (addr >= r.start && addr < r.end) return &r;
    return nullptr;
}

inline std::vector<const MemoryRegion*> FindRegionsByName(
        const std::vector<MemoryRegion>& regions, const std::string& substring) {
    std::vector<const MemoryRegion*> hits;
    for (const auto& r : regions)
        if (substring.empty() || r.pathname.find(substring) != std::string::npos)
            hits.push_back(&r);
    return hits;
}

// -----------------------------------------------------------------------------
// 3. SafeRead — cross-process memory read with region validation
// -----------------------------------------------------------------------------
//
// Reads from the TARGET process (target_pid). Uses process_vm_readv.
// If target_pid == getpid(), falls back to memcpy (self-introspection).
// Always validates the address against the provided maps.
inline size_t SafeRead(uint64_t addr, size_t size, void* out,
                       const std::vector<MemoryRegion>& maps) {
    if (!out || size == 0) return 0;
    if (target_pid <= 0) return 0; // no target

    size_t total = 0;
    uint8_t* dst = static_cast<uint8_t*>(out);

    while (total < size) {
        const uint64_t cur   = addr + total;
        const MemoryRegion* r = FindRegionForAddress(maps, cur);
        if (!r || !r->IsReadable()) break;

        const uint64_t room   = r->end - cur;
        const size_t   chunk  = static_cast<size_t>(
                room < (size - total) ? room : (size - total));

        // If reading self, use memcpy; otherwise use process_vm_readv.
        if (target_pid == getpid()) {
            std::memcpy(dst + total, reinterpret_cast<const void*>(cur), chunk);
        } else {
            struct iovec local  = { dst + total, chunk };
            struct iovec remote = { (void*)cur, chunk };
            ssize_t n = process_vm_readv(target_pid, &local, 1, &remote, 1, 0);
            if (n <= 0) break; // failed
            // n might be less than chunk; continue loop.
            if ((size_t)n < chunk) {
                // partial read, we can still move forward but we need to adjust.
                // We'll just break to avoid mixing; better to treat as failure.
                break;
            }
        }
        total += chunk;
    }
    return total;
}

template <typename T>
inline bool SafeReadValue(uint64_t addr, T* out,
                          const std::vector<MemoryRegion>& maps) {
    return SafeRead(addr, sizeof(T), out, maps) == sizeof(T);
}

// -----------------------------------------------------------------------------
// 4. Signature scanning — find a byte pattern in target process
// -----------------------------------------------------------------------------
//
// Scans the target process's writable private regions.
inline uint64_t ScanForPattern(const std::vector<MemoryRegion>& regions,
                               const void* pattern, size_t patternSize,
                               size_t align,
                               const std::function<bool(uint64_t)>& verify) {
    if (!pattern || patternSize == 0 || align == 0) return 0;
    if (target_pid <= 0) return 0;

    for (const auto& r : regions) {
        if (!r.IsReadable() || !r.IsWritable() || !r.IsPrivate()) continue;
        if (r.Size() < patternSize) continue;
        const uint64_t first = r.start + ((align - (r.start % align)) % align);
        for (uint64_t addr = first; addr + patternSize <= r.end; addr += align) {
            // Read patternSize bytes from target using SafeRead (but we can't call SafeRead inside loop efficiently; we'll read chunk)
            // Instead, read the pattern bytes into a local buffer and compare.
            uint8_t buf[128]; // assuming pattern size <= 128
            if (patternSize > sizeof(buf)) return 0; // too large
            if (SafeRead(addr, patternSize, buf, regions) != patternSize) continue;
            if (std::memcmp(buf, pattern, patternSize) != 0) continue;
            if (verify && verify(addr)) return addr;
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// 5. module base address finder
// -----------------------------------------------------------------------------
inline uintptr_t GetModuleBase(pid_t pid, const char* name) {
    if (pid <= 0) return 0;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, name)) {
            uintptr_t start;
            sscanf(line, "%lx-%*lx", &start);
            fclose(f);
            return start;
        }
    }
    fclose(f);
    return 0;
}

// -----------------------------------------------------------------------------
// 6. helpers for self (optional, kept for diagnostics)
// -----------------------------------------------------------------------------
inline int SelfPid() { return static_cast<int>(getpid()); }
inline std::string SelfCmdline() {
    std::FILE* f = std::fopen("/proc/self/cmdline", "rb");
    if (!f) return {};
    std::string s;
    int c;
    while ((c = std::fgetc(f)) != EOF && c != '\0') s.push_back(static_cast<char>(c));
    std::fclose(f);
    return s;
}

} // namespace mem
