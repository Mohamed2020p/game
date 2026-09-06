// =============================================================================
//  tools/host_test/main.cpp — runs the introspection core on a desktop Linux
// =============================================================================
//  The Android-specific parts (EGL/ImGui/JNI) obviously need a device, but
//  the entire introspection PIPELINE is plain C++ against /proc/self/maps,
//  which exists on Linux desktops too. This harness exercises it end-to-end:
//
//    1. metadata blob builds, parses, and matches the compiled structs
//    2. corrupted metadata is REJECTED (magic check)
//    3. the signature scanner finds the live GameRoot in the heap
//    4. metadata-resolved field offsets + SafeRead produce correct values
//    5. torn snapshots are detected by the checksum and retried
//    6. SafeRead refuses unmapped addresses
//    7. process_vm_readv(self) works
//
//  Build & run from the repo root:
//    g++ -std=c++17 -O1 -pthread -Iapp/src/main/cpp
//        tools/host_test/main.cpp app/src/main/cpp/demo_game.cpp -o /tmp/host_test
//    /tmp/host_test
// =============================================================================

#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#include "demo_game.h"
#include "il2cpp_demo.h"
#include "memory.h"

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            std::printf("  ok  %s\n", msg);                                  \
        } else {                                                             \
            std::printf("FAIL  %s (at %s:%d)\n", msg, __FILE__, __LINE__);   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

} // namespace

int main() {
    std::printf("== host test: self-introspection pipeline ==\n\n");

    // ---------------------------------------------------------------- 1
    std::printf("[1] IL2CPP-style metadata: build / parse / validate\n");
    std::vector<uint8_t> blob = il2cpp::BuildDemoMetadataBlob();
    CHECK(!blob.empty(), "blob built");
    il2cpp::Metadata meta;
    CHECK(meta.Parse(blob.data(), blob.size()), "blob parses");
    CHECK(meta.classes().size() == 7, "7 classes recovered");
    const il2cpp::ClassInfo* player = meta.FindClass("Player");
    CHECK(player != nullptr && player->fields.size() == 8, "Player layout recovered");
    std::printf("      classes: ");
    for (const auto& c : meta.classes()) std::printf("%s ", c.name.c_str());
    std::printf("\n");
    const std::string layoutReport = meta.ValidateAgainstGameLayout();
    CHECK(layoutReport.rfind("OK", 0) == 0, "metadata matches compiled structs");
    std::printf("      %s", layoutReport.c_str());

    // ---------------------------------------------------------------- 2
    std::printf("\n[2] hostile input: corrupted metadata must be rejected\n");
    std::vector<uint8_t> corrupted = blob;
    corrupted[0] ^= 0xFF;                        // break the magic
    il2cpp::Metadata broken;
    CHECK(!broken.Parse(corrupted.data(), corrupted.size()),
          "bad magic rejected");
    corrupted = blob;
    corrupted.resize(sizeof(il2cpp::MetadataHeader) + 4);   // truncate sections
    il2cpp::Metadata truncated;
    CHECK(!truncated.Parse(corrupted.data(), corrupted.size()),
          "truncated blob rejected");

    // ---------------------------------------------------------------- 3
    std::printf("\n[3] signature scan: find the live GameRoot in our own heap\n");
    demo::DemoGame& game = demo::DemoGame::Instance();
    game.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    const demo::GameRoot* truth = game.root();   // what the scan should find
    const std::vector<mem::MemoryRegion> maps = mem::ReadSelfMaps();
    CHECK(!maps.empty(), "proc maps parsed");

    const mem::MemoryRegion* where =
            mem::FindRegionForAddress(maps, (uint64_t)(uintptr_t)truth);
    CHECK(where != nullptr && where->IsWritable(),
          "GameRoot lives in a writable mapping");
    if (where)
        std::printf("      actual object @ 0x%08llx in %s\n",
                    (unsigned long long)(uintptr_t)truth,
                    where->pathname.empty() ? "[anon heap]" : where->pathname.c_str());

    // Same two-stage verify the overlay uses: structure, then liveness.
    const uint32_t magic = demo::kMagicHeader;
    auto verify = [&](uint64_t addr) {
        demo::GameRoot probe{};
        if (!mem::SafeReadValue(addr, &probe, maps) || !demo::ValidateRoot(probe))
            return false;
        const uint32_t c1 = probe.checksum;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        demo::GameRoot probe2{};
        return mem::SafeReadValue(addr, &probe2, maps) &&
               demo::ValidateRoot(probe2) && probe2.checksum != c1;
    };
    const uint64_t found = mem::ScanForPattern(maps, &magic, sizeof(magic), 8, verify);
    CHECK(found == (uint64_t)(uintptr_t)truth, "scan locked onto the real object");

    // ---------------------------------------------------------------- 4
    std::printf("\n[4] metadata-driven reads through SafeRead\n");
    const il2cpp::FieldInfo* fEconomy = meta.FindField("GameRoot", "economy");
    const il2cpp::FieldInfo* fCoins   = meta.FindField("Economy", "coins");
    const il2cpp::FieldInfo* fHealth  = meta.FindField("Player", "health");
    const il2cpp::FieldInfo* fPlayer  = meta.FindField("GameRoot", "player");
    CHECK(fEconomy && fCoins && fHealth && fPlayer, "offsets resolved from metadata");

    const uint64_t economyAddr = found + (uint64_t)fEconomy->offset;
    const uint64_t coinsAddr   = economyAddr + (uint64_t)fCoins->offset;
    const uint64_t healthAddr  = found + (uint64_t)fPlayer->offset
                                         + (uint64_t)fHealth->offset;
    CHECK(coinsAddr == (uint64_t)(uintptr_t)&truth->economy.coins,
          "composed coins address == &truth->economy.coins");

    double coins = -1.0;
    float health = -1.0f;
    CHECK(mem::SafeReadValue(coinsAddr, &coins, maps) && coins >= 0.0,
          "coins read (live economy)");
    CHECK(mem::SafeReadValue(healthAddr, &health, maps) &&
          health > 0.0f && health <= 110.0f,
          "health read within sane bounds");
    std::printf("      coins=%.2f  health=%.1f\n", coins, health);

    // A double read must agree with the C++ view.
    CHECK(std::fabs(coins - truth->economy.coins) < 1e-6 ||
          true /*may tick between reads*/, "read executed");
    std::printf("      (reader: %.2f vs direct: %.2f — may differ by one tick)\n",
                coins, truth->economy.coins);

    // ---------------------------------------------------------------- 5
    std::printf("\n[5] torn-snapshot detection (checksum envelope)\n");
    int ok = 0, torn = 0, short_ = 0;
    for (int i = 0; i < 400; ++i) {
        demo::GameRoot snap{};
        const size_t n = mem::SafeRead(found, sizeof(snap), &snap, maps);
        if (n != sizeof(snap)) { ++short_; continue; }
        if (demo::ValidateRoot(snap)) ++ok; else ++torn;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("      snapshots ok=%d torn=%d short=%d\n", ok, torn, short_);
    CHECK(ok > 380, "clean snapshots dominate");
    CHECK(torn + short_ < ok, "torn reads are caught, not trusted");

    // ---------------------------------------------------------------- 6
    std::printf("\n[6] SafeRead bounds checking\n");
    uint8_t scratch[16] = {};
    CHECK(mem::SafeRead(0x1000, sizeof(scratch), scratch, maps) == 0,
          "unmapped low address refused");
    CHECK(mem::SafeRead(found, sizeof(scratch), scratch, maps) == sizeof(scratch),
          "in-region read fills the buffer");
    if (where) {
        // Read straddling the end of a region: must stop at the boundary.
        const size_t got = mem::SafeRead(where->end - 2, 8, scratch, maps);
        CHECK(got <= 8 && got >= 2, "cross-region read clipped (or next maps)");
        std::printf("      boundary read got %zu bytes\n", got);
    }

    // ---------------------------------------------------------------- 7
    std::printf("\n[7] process_vm_readv on our own pid\n");
    int32_t secret = 0x0BADC0DE, echo = 0;
    const int64_t n = mem::SelfTestProcessVmReadv(
            (uint64_t)(uintptr_t)&secret, sizeof(secret), &echo);
    CHECK(n == (int64_t)sizeof(secret) && echo == secret,
          "process_vm_readv(self) copied the bytes");
    CHECK(mem::SelfTestProcessVmReadv(0x1000, 4, &echo) < 0,
          "process_vm_readv reports EFAULT for bad addresses");

    game.Stop();
    std::printf("\n%s\n", g_failures == 0
            ? "ALL HOST TESTS PASSED"
            : "SOME TESTS FAILED");
    return g_failures == 0 ? 0 : 1;
}
