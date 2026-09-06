// =============================================================================
//  memory.h — educational memory introspection primitives
// =============================================================================
//
//  This header teaches the foundations every memory-inspection tool is built
//  on, using the ONE process we are allowed to inspect freely: our own.
//
//  Concepts covered, in reading order:
//
//   1. /proc/self/maps        — the kernel's per-process address-space map.
//   2. Region parsing         — turning text lines into typed records.
//   3. Bounds-checked reads   — "SafeRead": never touch an address without
//                               first proving a readable mapping covers it.
//   4. Signature scanning     — finding a known byte pattern in the heap,
//                               the way value-scanners locate objects.
//   5. process_vm_readv       — the syscall family used for cross-process
//                               reads, demonstrated on our own PID.
//
//  A note on scope, because this matters:
//
//  The SAME parsing code works on /proc/<pid>/maps of any process. What
//  differs is *reading* another process's memory: that requires privileges
//  you do not have as a normal app (same-UID + ptrace-eligible, or root).
//  This project deliberately keeps every read inside this process — it is
//  the honest way to learn the machinery without building a tool whose
//  main use is tampering with software you don't own. Where relevant, the
//  comments point out exactly what would (and legally may not) change.
//
//  Everything here is plain C++17 and also compiles on a desktop Linux
//  box (see tools/host_test/) because /proc/self/maps exists there too.
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

namespace mem {

// -----------------------------------------------------------------------------
// 1. The region record — one line of /proc/<pid>/maps
// -----------------------------------------------------------------------------
//
// A maps line looks like:
//
//   7f2a4c000000-7f2a4c021000 rw-p 00000000 103:04 262144  [anon:libc_malloc]
//   ^ start        ^ end        ^perms^offset^dev    ^inode ^pathname
//
// Column meanings (see proc(5) "maps"):
//   start-end : virtual address range covered by the mapping.
//   perms     : r/w/x/s flags + 'p' (private) or 's' (shared).
//   offset    : offset into the mapped file (0 for anonymous memory).
//   dev/inode : the file backing the mapping (0:0 / 0 for anonymous).
//   pathname  : file path or a kernel tag like [heap], [stack], [anon:...].
//
// Why we care: this file is the ground truth of what we may dereference.
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

    // Anonymous memory = RAM with no file behind it. That's where malloc'd
    // C++ objects live (the ART/Unity heap is anonymous or [anon:...] memory),
    // which is exactly where our signature scan should look.
    bool IsAnonymous() const {
        return pathname.empty() ||
               pathname.rfind("[anon:", 0) == 0 ||
               pathname == "[heap]" ||
               pathname == "[stack]";
    }
};

// Parse one maps line. Returns false on malformed input (never throws).
inline bool ParseMapsLine(const char* line, MemoryRegion* out) {
    if (!line || !out) return false;
    *out = MemoryRegion{};

    unsigned long long start = 0, end = 0, off = 0, ino = 0;
    char perms[8] = {0};
    char dev[24]  = {0};

    // %n tracks where the fixed columns ended so we can grab the (optional)
    // pathname with all of its internal spaces preserved.
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

    // Trim the trailing newline; keep interior spaces ("[anon:scudo:...]" etc).
    std::string rest(line + consumed);
    while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r'))
        rest.pop_back();
    out->pathname = rest;
    return true;
}

// Read and parse the whole maps file of any process we are allowed to read.
// For our own process this always succeeds; for another PID the kernel gates
// it by ptrace rules (same UID, Yama policy, debuggable, or root).
inline std::vector<MemoryRegion> ReadMaps(pid_t pid) {
    std::vector<MemoryRegion> regions;
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    std::FILE* f = std::fopen(path, "r");
    if (!f) return regions;                    // e.g. EACCES for foreign pids

    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        MemoryRegion r;
        if (ParseMapsLine(line, &r)) regions.push_back(std::move(r));
    }
    std::fclose(f);
    return regions;
}

// The educational main character: our own address space.
inline std::vector<MemoryRegion> ReadSelfMaps() { return ReadMaps(getpid()); }

// -----------------------------------------------------------------------------
// 2. Region lookup helpers
// -----------------------------------------------------------------------------

// Which mapping (if any) covers this address?  Linear scan is fine: maps has
// on the order of a few hundred lines and is already sorted by address.
inline const MemoryRegion* FindRegionForAddress(
        const std::vector<MemoryRegion>& regions, uint64_t addr) {
    for (const auto& r : regions)
        if (addr >= r.start && addr < r.end) return &r;
    return nullptr;
}

// All regions whose pathname contains `substring` (e.g. "[anon:", "libil2cpp",
// "[stack]").  Empty substring returns every region.
inline std::vector<const MemoryRegion*> FindRegionsByName(
        const std::vector<MemoryRegion>& regions, const std::string& substring) {
    std::vector<const MemoryRegion*> hits;
    for (const auto& r : regions)
        if (substring.empty() || r.pathname.find(substring) != std::string::npos)
            hits.push_back(&r);
    return hits;
}

// -----------------------------------------------------------------------------
// 3. SafeRead — the single most important habit in memory tooling
// -----------------------------------------------------------------------------
//
// Rule #1 of reading raw addresses: NEVER trust the pointer. An address can be
// stale (object freed/moved), garbage (wrong offset math), or kernel space.
// On a *foreign* process a bad address segfaults your reader; here it would
// segfault us, so we prove every byte is inside a readable mapping BEFORE the
// first load. This is the same guarantee tools like Cheat Engine give their
// UI thread by using process_vm_readv (which fails with EFAULT instead of
// crashing) — we get it with zero syscalls because the maps file tells us.
//
// Contract:
//   * reads may span multiple adjacent regions; the copy stops at the first
//     unreadable byte and reports how much was actually read
//   * out is left untouched unless bytes were read
//   * no exception leaves this function
inline size_t SafeRead(uint64_t addr, size_t size, void* out,
                       const std::vector<MemoryRegion>& maps) {
    if (!out || size == 0) return 0;

    size_t total = 0;
    uint8_t* dst = static_cast<uint8_t*>(out);

    while (total < size) {
        const uint64_t cur   = addr + total;
        const MemoryRegion* r = FindRegionForAddress(maps, cur);
        if (!r || !r->IsReadable()) break;             // unmapped / guarded

        // Never walk past the end of THIS region: the next page may be a gap.
        const uint64_t room   = r->end - cur;
        const size_t   chunk  = static_cast<size_t>(
                room < (size - total) ? room : (size - total));

        // Same process => a plain memcpy is valid *because* we just proved the
        // range is mapped. (Cross-process tools use process_vm_readv here; see
        // SelfTestProcessVmReadv below for what that looks like.)
        std::memcpy(dst + total, reinterpret_cast<const void*>(cur), chunk);
        total += chunk;
    }
    return total;
}

// Typed convenience wrapper. Returns true iff the full value was read.
template <typename T>
inline bool SafeReadValue(uint64_t addr, T* out,
                          const std::vector<MemoryRegion>& maps) {
    return SafeRead(addr, sizeof(T), out, maps) == sizeof(T);
}

// -----------------------------------------------------------------------------
// 4. Signature scanning — "find this byte pattern in memory"
// -----------------------------------------------------------------------------
//
// This is how every value-scanner works: pick bytes that identify the object
// (a magic constant, a vtable pointer, an exact value), walk candidate memory
// at the right alignment, then VERIFY each hit with SafeRead + a secondary
// check so random matches don't fool you.
//
//   pattern : raw bytes to look for (here: a 4-byte magic tag)
//   align   : candidate step. Heap objects of our kind are 8-aligned; using
//             8 instead of 1 is an 8x speedup and mirrors how real scanners
//             exploit known allocator alignment.
//   verify  : second-stage check on a candidate ADDRESS (pattern hit is at
//             addr). Read whatever structures you need with SafeRead and
//             return true only if the whole record is consistent.
//
// Returns the first verified hit or 0. We only scan WRITABLE PRIVATE regions:
// the object we hunt is a live heap allocation, which is exactly where it can
// live. (Read-only / file-backed regions can't contain it and are skipped.)
inline uint64_t ScanForPattern(const std::vector<MemoryRegion>& regions,
                               const void* pattern, size_t patternSize,
                               size_t align,
                               const std::function<bool(uint64_t)>& verify) {
    if (!pattern || patternSize == 0 || align == 0) return 0;

    // FNV-1a of the pattern isn't needed; a memcmp per candidate is already
    // fast at alignment 8 over a few MB of heap. Keep it simple = keep it clear.
    for (const auto& r : regions) {
        if (!r.IsReadable() || !r.IsWritable() || !r.IsPrivate()) continue;
        if (r.Size() < patternSize) continue;

        // Snap the walk to alignment: first aligned address >= region start.
        const uint64_t first = r.start +
                ((align - (r.start % align)) % align);

        for (uint64_t addr = first; addr + patternSize <= r.end; addr += align) {
            const auto* candidate =
                    reinterpret_cast<const uint8_t*>(addr); // NOLINT
            if (std::memcmp(candidate, pattern, patternSize) != 0) continue;

            // Pattern hit -> demand full structural validation. This two-stage
            // design (cheap filter, expensive verify) is the standard scanner
            // architecture; the verify stage is where false positives die.
            if (verify && verify(addr)) return addr;
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// 5. process_vm_readv — the "proper" cross-process read syscall, on ourselves
// -----------------------------------------------------------------------------
//
// process_vm_readv/pwritev (Linux 3.2+, Android API 23+) copy memory between
// processes subject to ptrace access rules. It is the syscall behind every
// legitimate debugger attached to a process you control (lldb, perf, CRMs of
// your own apps) — and also behind every rooted memory editor. We call it on
// our own PID purely so you can see the API shape, its iov encoding, and its
// error contract (returns -1/EFAULT for bad addresses instead of crashing).
//
// Returns bytes read (>=0) or -errno (<0) on failure.
inline int64_t SelfTestProcessVmReadv(uint64_t addr, size_t size, void* out) {
    if (!out || size == 0) return -EINVAL;

    // Local iovec = destination in OUR address space.
    struct ::iovec local  { out, size };
    // Remote iovec = source (address, size) in the TARGET's space; here the
    // target is getpid(), so "remote" is just... us. That's the whole lesson:
    // the API doesn't care, the kernel's permission check does.
    struct ::iovec remote { reinterpret_cast<void*>(addr), size }; // NOLINT

    const ssize_t n = ::process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    if (n >= 0) return static_cast<int64_t>(n);
    return -errno;   // e.g. -EFAULT for unmapped, -EPERM for foreign+denied
}

// -----------------------------------------------------------------------------
// Small self-identity helpers — what process are we, exactly?
// -----------------------------------------------------------------------------

inline int SelfPid() { return static_cast<int>(getpid()); }

// /proc/self/cmdline — the name other tools would use to FIND this process.
// (Enumerating /proc/<pid>/cmdline for all pids is how process lists work;
// finding a *target* by name is trivial — the hard part, and the part this
// project deliberately stops at, is doing anything to a process that isn't
// yours. See the README's scope section.)
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
