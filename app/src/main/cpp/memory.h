// Add these to memory.h (after SafeRead)

// -----------------------------------------------------------------------------
// Write memory — cross-process memory write
// -----------------------------------------------------------------------------
inline size_t SafeWrite(uint64_t addr, size_t size, const void* data,
                        const std::vector<MemoryRegion>& maps) {
    if (!data || size == 0) return 0;
    if (target_pid <= 0) return 0;

    size_t total = 0;
    const uint8_t* src = static_cast<const uint8_t*>(data);

    while (total < size) {
        const uint64_t cur = addr + total;
        const MemoryRegion* r = FindRegionForAddress(maps, cur);
        if (!r || !r->IsWritable()) break;

        const uint64_t room = r->end - cur;
        const size_t chunk = static_cast<size_t>(
            room < (size - total) ? room : (size - total));

        if (target_pid == getpid()) {
            std::memcpy(reinterpret_cast<void*>(cur), src + total, chunk);
        } else {
            struct iovec local = { (void*)(src + total), chunk };
            struct iovec remote = { (void*)cur, chunk };
            ssize_t n = process_vm_writev(target_pid, &local, 1, &remote, 1, 0);
            if (n <= 0) break;
        }
        total += chunk;
    }
    return total;
}

template <typename T>
inline bool WriteMemoryValue(uint64_t addr, const T& value,
                             const std::vector<MemoryRegion>& maps) {
    return SafeWrite(addr, sizeof(T), &value, maps) == sizeof(T);
}
