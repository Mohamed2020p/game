// =============================================================================
//  demo_game.cpp — simulation implementation
// =============================================================================
//  A tiny deterministic-ish idle economy. Every value the overlay displays
//  moves on its own so you can SEE introspection working: sparklines change,
//  counters tick, the checksum validation flips between pass/teardown as the
//  tick thread races readers (watch the Misc tab).
// =============================================================================

#include "demo_game.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <thread>

// Logging: android logcat on device, stdout on the host test build.
#if defined(__ANDROID__)
#include <android/log.h>
#define DEMO_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "DemoGame", __VA_ARGS__)
#else
#define DEMO_LOGI(...) do { std::fprintf(stdout, "[DemoGame] "); \
                            std::fprintf(stdout, __VA_ARGS__); \
                            std::fprintf(stdout, "\n"); } while (0)
#endif

namespace demo {

// -----------------------------------------------------------------------------
// Checksum / validation
// -----------------------------------------------------------------------------

// FNV-1a 32-bit over a byte range. Not cryptographic — it exists to detect
// *torn reads* (reader memcpy'd while the tick thread wrote), not adversaries.
uint32_t ChecksumRange(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t ComputeChecksum(const GameRoot& root) {
    // Everything from the start of the struct up to (not including) the footer:
    // header + player + vehicles + ores + workers + economy + settings.
    return ChecksumRange(&root, offsetof(GameRoot, footer));
}

bool ValidateRoot(const GameRoot& root) {
    if (root.header.magic != kMagicHeader) return false;
    if (root.header.layoutVersion != kLayoutVersion) return false;
    if (root.header.payloadSize != sizeof(GameRoot)) return false;
    if (root.footer != kMagicFooter) return false;
    return root.checksum == ComputeChecksum(root);
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

DemoGame& DemoGame::Instance() {
    // Deliberately heap-allocated ('new', never deleted): the whole premise of
    // the introspection demo is "find the live object in the HEAP by scanning
    // its bytes". A function-local static would sit in the library's .bss
    // instead — still findable, but this keeps the teaching story exact, and
    // the anonymous-rw heap regions are what the scanner walks.
    static DemoGame* instance = new DemoGame();
    return *instance;
}

DemoGame::DemoGame() {
    std::memset(&root_, 0, sizeof(root_));

    root_.header.magic         = kMagicHeader;
    root_.header.layoutVersion = kLayoutVersion;
    root_.header.payloadSize   = sizeof(GameRoot);
    root_.header.reserved      = 0;

    // --- Player ---
    root_.player = {/*health*/ 86.0f, /*max*/ 100.0f, /*stamina*/ 0.8f,
                    /*posX*/ 0.0f, /*posY*/ 0.0f, /*speed*/ 4.2f,
                    /*level*/ 1, /*xp*/ 0};

    // --- Vehicles ---
    const char* vehicleNames[4] = {"Pickup", "Hauler", "Drill Rig", "Loader"};
    for (int i = 0; i < 4; ++i) {
        Vehicle& v = root_.vehicles[i];
        std::strncpy(v.name, vehicleNames[i], sizeof(v.name) - 1);
        v.fuel       = 40.0f + 15.0f * i;
        v.fuelMax    = 60.0f + 10.0f * i;
        v.speedMul   = 1.0f + 0.1f * i;
        v.engineOn   = (i % 2) == 0;
        v.refuelCount = 0;
    }

    // --- Ore veins ---
    const char* oreNames[5]   = {"Iron", "Copper", "Gold", "Crystal", "Uranium"};
    const float oreRich[5]    = {2.0f, 1.4f, 0.5f, 0.25f, 0.1f};
    const int   oreTier[5]    = {1, 1, 2, 3, 4};
    const float orePrice[5]   = {0.8f, 1.5f, 6.0f, 18.0f, 55.0f};
    for (int i = 0; i < 5; ++i) {
        OreVein& o = root_.ores[i];
        std::strncpy(o.name, oreNames[i], sizeof(o.name) - 1);
        o.amount    = 0.0f;
        o.richness  = oreRich[i];
        o.tier      = oreTier[i];
        o.price     = orePrice[i];
    }

    // --- Workers: 8 hires spread over the veins ---
    for (int i = 0; i < 8; ++i) {
        Worker& w = root_.workers[i];
        std::snprintf(w.name, sizeof(w.name), "Worker-%d", i + 1);
        w.state      = kWorkerMining;
        w.efficiency = 0.55f + 0.05f * i;              // 0.55 .. 0.90
        w.fatigue    = 0.1f * i;
        w.oreIndex   = i % 5;
    }

    // --- Economy / settings ---
    root_.economy.coins         = 1250.0;
    root_.economy.gems          = 12;
    root_.economy.incomePerSec  = 0.0f;
    root_.economy.pendingIncome = 0.0;

    root_.settings.timeScale       = 1.0f;
    root_.settings.moveSpeedMul    = 1.0f;
    root_.settings.vehicleSpeedMul = 1.0f;
    root_.settings.autoSmelt       = 1;
    root_.settings.staminaBoost    = 0;
    root_.settings.fastHaul        = 0;

    root_.footer   = kMagicFooter;
    root_.checksum = ComputeChecksum(root_);
}

DemoGame::~DemoGame() { Stop(); }

void DemoGame::Start() {
    if (running_.load()) return;
    running_.store(true, std::memory_order_release);
    std::thread([this] { TickLoop(); }).detach();  // demo-grade lifecycle; a
    // production service would join this thread properly in Stop().
    DEMO_LOGI("demo game started, sizeof(GameRoot)=%zu, checksum=0x%08x",
         sizeof(GameRoot), root_.checksum);
}

void DemoGame::Stop() {
    if (!running_.exchange(false)) return;
    // Give the detached tick thread time to observe running_==false and exit.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    DEMO_LOGI("demo game stopped after %" PRIu64 " ticks",
         tickCount_.load(std::memory_order_relaxed));
}

// -----------------------------------------------------------------------------
// The tick loop — 10 Hz wall clock, scaled internally by settings.timeScale
// -----------------------------------------------------------------------------

void DemoGame::TickLoop() {
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    const auto step = std::chrono::milliseconds(100);

    while (running_.load(std::memory_order_acquire)) {
        next += step;
        Tick(0.1f * root_.settings.timeScale);
        tickCount_.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_until(next);
    }
}

void DemoGame::Tick(float dt) {
    if (dt <= 0.0f) return;
    simTime_ += dt;

    // dt is *simulated* seconds per tick (0.1s wall x timeScale).
    // "step" is just the dimensionless speed factor: dt / 0.1s.
    const float t    = static_cast<float>(simTime_);
    const float step = dt * 10.0f;

    // ----- Player: wanders a circle, regenerates, levels up -----
    Player& p = root_.player;
    const float speed = p.baseMoveSpeed * root_.settings.moveSpeedMul;
    p.posX = 24.0f * std::cos(t * 0.15f * speed * 0.25f);
    p.posY = 24.0f * std::sin(t * 0.15f * speed * 0.25f);
    p.stamina = std::min(1.0f,
            (0.45f + 0.45f * (0.5f + 0.5f * std::sin(t * 0.4f))) *
            (root_.settings.staminaBoost ? 1.35f : 1.0f));
    p.health += (p.maxHealth - p.health) * 0.2f * step;  // slow regen
    if (p.health > p.maxHealth) p.health = p.maxHealth;
    p.xp += static_cast<int32_t>(1.6f * step);
    p.level = 1 + p.xp / 250;

    // ----- Workers: mine / haul / rest loop -----
    double minedThisTick[5] = {0, 0, 0, 0, 0};
    const uint64_t tick = tickCount_.load(std::memory_order_relaxed);
    for (int i = 0; i < 8; ++i) {
        Worker& w = root_.workers[i];
        switch (w.state) {
            case kWorkerMining:
                w.fatigue += 0.055f * step;
                minedThisTick[w.oreIndex] +=
                        root_.ores[w.oreIndex].richness * w.efficiency * step;
                if (w.fatigue >= 1.0f) {
                    w.fatigue = 1.0f;
                    w.state = kWorkerResting;
                } else if (((tick + static_cast<uint64_t>(i)) % 90) == 0) {
                    w.state = kWorkerHauling;             // periodic haul run
                }
                break;
            case kWorkerHauling: {
                const float rate = root_.settings.fastHaul ? 2.0f : 1.0f;
                minedThisTick[w.oreIndex] += 0.4f * rate * step;
                w.fatigue += 0.02f * step;
                if ((tick % 25) == 0) w.state = kWorkerMining;
                break;
            }
            case kWorkerResting:
                w.fatigue -= 0.12f * step;
                if (w.fatigue <= 0.05f) {
                    w.fatigue = 0.05f;
                    w.state = kWorkerMining;
                }
                break;
            default:
                w.state = kWorkerMining;
                break;
        }
    }

    // ----- Ore veins accumulate what was mined -----
    for (int i = 0; i < 5; ++i) root_.ores[i].amount += (float)minedThisTick[i];

    // ----- Economy: smelting converts ore to coins -----
    Economy& e = root_.economy;
    double coinsThisTick = 0.0;
    if (root_.settings.autoSmelt) {
        for (int i = 0; i < 5; ++i)
            coinsThisTick += minedThisTick[i] * root_.ores[i].price * 0.1;
    }
    e.incomePerSec = static_cast<float>(coinsThisTick / dt);  // sim-time rate
    e.pendingIncome += coinsThisTick;

    // ----- Vehicles drain fuel while driving; auto-refuel when nearly dry -----
    for (int i = 0; i < 4; ++i) {
        Vehicle& v = root_.vehicles[i];
        if (!v.engineOn) continue;
        v.fuel -= (0.8f + v.speedMul * root_.settings.vehicleSpeedMul) * step * 0.35f;
        if (v.fuel <= v.fuelMax * 0.05f) {
            v.fuel = v.fuelMax;
            ++v.refuelCount;
        }
    }

    // Checksum is recomputed LAST so it always describes the final tick state.
    root_.checksum = ComputeChecksum(root_);
}

// -----------------------------------------------------------------------------
// The game's own API — the only path the overlay UI uses to CHANGE anything.
// -----------------------------------------------------------------------------

void DemoGame::SetTimeScale(float v) {
    root_.settings.timeScale = std::min(4.0f, std::max(0.25f, v));
}
void DemoGame::SetMoveSpeedMul(float v) {
    root_.settings.moveSpeedMul = std::min(4.0f, std::max(0.25f, v));
}
void DemoGame::SetVehicleSpeedMul(float v) {
    root_.settings.vehicleSpeedMul = std::min(4.0f, std::max(0.25f, v));
}
void DemoGame::SetWorkerEfficiency(float v) {
    for (auto& w : root_.workers)
        w.efficiency = std::min(1.0f, std::max(0.4f, v));
}
void DemoGame::SetAutoSmelt(bool on)    { root_.settings.autoSmelt = on ? 1 : 0; }
void DemoGame::SetStaminaBoost(bool on) { root_.settings.staminaBoost = on ? 1 : 0; }
void DemoGame::SetFastHaul(bool on)     { root_.settings.fastHaul = on ? 1 : 0; }

void DemoGame::SetEngineOn(int i, bool on) {
    if (i >= 0 && i < 4) root_.vehicles[i].engineOn = on ? 1 : 0;
}
void DemoGame::RefuelVehicle(int i) {
    if (i >= 0 && i < 4) root_.vehicles[i].fuel = root_.vehicles[i].fuelMax;
}
void DemoGame::CollectPendingIncome() {
    root_.economy.coins += root_.economy.pendingIncome;
    root_.economy.pendingIncome = 0.0;
}
void DemoGame::RespawnPlayer() {
    root_.player.health = root_.player.maxHealth;
    root_.player.stamina = 1.0f;
    root_.player.posX = root_.player.posY = 0.0f;
}

} // namespace demo
