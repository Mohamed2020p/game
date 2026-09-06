// =============================================================================
//  demo_game.h — the "game" that lives inside this app
// =============================================================================
//
//  To teach memory introspection honestly you need a *target*: live, changing
//  data at real heap addresses. Instead of pointing at someone else's process,
//  this app embeds its own tiny "game" simulation — a mining/idle economy with
//  a player, vehicles, ore veins and workers — and the overlay reads THAT
//  through the exact techniques a memory reader uses:
//
//      /proc/self/maps  ->  signature scan  ->  checksum validation
//                       ->  metadata-resolved field offsets ->  SafeRead
//
//  Design notes that matter for the lesson:
//
//   * All structs are plain old data (no vtables, no pointers, no std::string).
//     That gives them fixed, predictable layouts — which is what makes
//     "field offset" tables possible at all. (In real IL2CPP games the layouts
//     come from the compiled metadata; see il2cpp_demo.h.)
//
//   * The header/footer/checksum envelope lets the reader PROVE it found the
//     real object and that a snapshot wasn't torn mid-tick. This is the same
//     discipline real tools use; without it you read garbage.
//
//   * The overlay NEVER writes to these structures. Mutations go through the
//     class's own methods (SetTimeScale, RefuelVehicle, ...) — i.e. through
//     code, the way the game itself changes state. Reads-only introspection
//     plus public API calls keeps this a clean teaching tool.
//
//   * The tick thread mutates while readers read. We do NOT take a lock.
//     Small scalar reads are atomic-ish in practice, bigger ones can tear —
//     and the checksum envelope is what catches that. (Commented below, and
//     re-validated live in the Misc tab: a great thing to watch happen.)
// =============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace demo {

// --- Envelope constants (what the scanner hunts for) ------------------------
constexpr uint32_t kMagicHeader  = 0x44474D31u;  // 'DGM1' — Demo Game Magic 1
constexpr uint32_t kMagicFooter  = 0x44474D39u;  // 'DGM9' — mirrored footer
constexpr uint32_t kLayoutVersion = 3u;          // bump when struct layout changes

// --- Player ------------------------------------------------------------------
struct Player {
    float   health;        // 0..maxHealth
    float   maxHealth;     // 100
    float   stamina;       // 0..1, oscillates as the player "moves"
    float   posX;          // player wanders on a circle
    float   posY;
    float   baseMoveSpeed; // units/sec, before multipliers
    int32_t level;         // grows with xp
    int32_t xp;            // accumulates over time
};

// --- Vehicles ------------------------------------------------------------------
struct Vehicle {
    char    name[16];      // "Pickup", "Hauler", ...
    float   fuel;          // 0..fuelMax, drains while engineOn
    float   fuelMax;
    float   speedMul;      // per-vehicle speed multiplier
    int32_t engineOn;      // 0 / 1 (int32 keeps every field 4-aligned — easy
                           // offsets to reason about in the metadata viewer)
    int32_t refuelCount;   // how many automatic refuels happened (fun to watch)
};

// --- Ore -----------------------------------------------------------------------
struct OreVein {
    char    name[16];      // "Iron", "Copper", "Gold", "Crystal", "Uranium"
    float   amount;        // units extracted so far (grows while miners work)
    float   richness;      // units/sec per miner at efficiency 1.0
    int32_t tier;          // 1..5 — mostly cosmetic, shows in the UI
    float   price;         // coins per unit when hauled/smelted
};

// --- Workers -------------------------------------------------------------------
enum WorkerState : int32_t {
    kWorkerIdle    = 0,
    kWorkerMining  = 1,
    kWorkerHauling = 2,
    kWorkerResting = 3,
};

struct Worker {
    char    name[16];
    int32_t state;         // WorkerState
    float   efficiency;    // 0.4 .. 1.0
    float   fatigue;       // 0..1, rises mining, falls resting
    int32_t oreIndex;      // which OreVein this worker mines
};

// --- Economy -------------------------------------------------------------------
struct Economy {
    double  coins;         // the number every economy tab in every game shows
    int32_t gems;          // premium currency (static here)
    float   incomePerSec;  // derived from mining + hauling rates
    double  pendingIncome; // accumulates until "Collect" is pressed
};

// --- Demo behavior settings (changed ONLY via the game's own API) ---------------
struct GameSettings {
    float   timeScale;        // global simulation speed
    float   moveSpeedMul;     // player speed multiplier
    float   vehicleSpeedMul;  // global vehicle multiplier
    int32_t autoSmelt;        // ore converts to coins automatically
    int32_t staminaBoost;     // faster stamina regen
    int32_t fastHaul;         // workers haul at 2x
};

// --- Envelope + root -------------------------------------------------------------
// The whole state is ONE contiguous heap allocation, bracketed by the envelope.
struct GameHeader {
    uint32_t magic;          // kMagicHeader — scan anchor
    uint32_t layoutVersion;  // kLayoutVersion — rejects old layouts
    uint32_t payloadSize;    // sizeof(GameRoot) — rejects layout drift
    uint32_t reserved;       // zero
};

struct GameRoot {
    GameHeader  header;      // offsetof = 0, so the scan hit IS the object base
    Player      player;
    Vehicle     vehicles[4];
    OreVein     ores[5];
    Worker      workers[8];
    Economy     economy;
    GameSettings settings;
    uint32_t    footer;      // kMagicFooter — part of validation
    uint32_t    checksum;    // FNV-1a over [begin .. footer) — torn-read guard
};

// Layout sanity: these asserts double as documentation of the binary layout
// the metadata in il2cpp_demo.h must mirror. If you add a field, bump
// kLayoutVersion and update il2cpp_demo's field tables to match — the asserts
// (and the overlay's own validation) will tell you if you forgot.
static_assert(sizeof(GameHeader) == 16, "GameHeader must stay 16 bytes");
static_assert(std::is_standard_layout<GameRoot>::value,
              "GameRoot must be standard-layout for offsetof()/SafeRead use");
static_assert(sizeof(GameRoot) < 4096, "keep GameRoot small enough to snapshot");

// FNV-1a over a byte range — tiny, order-dependent, good enough to catch tears.
uint32_t ChecksumRange(const void* data, size_t size);

// Compute what root.checksum SHOULD be for the current contents.
uint32_t ComputeChecksum(const GameRoot& root);

// True iff header/footer/version/size/checksum all agree. This is the exact
// validation the memory reader runs on every candidate + every snapshot.
bool ValidateRoot(const GameRoot& root);

// -----------------------------------------------------------------------------
// DemoGame — owns the state, runs the simulation, exposes the game's own API.
// -----------------------------------------------------------------------------
class DemoGame {
public:
    static DemoGame& Instance();

    void Start();                 // spawns the 10 Hz tick thread
    void Stop();                  // joins it
    bool IsRunning() const { return running_.load(std::memory_order_acquire); }
    uint64_t TickCount() const { return tickCount_.load(std::memory_order_relaxed); }

    // Pointer to the live object. In the overlay's read path this is NOT used —
    // the overlay finds the object by scanning memory, like a real reader. This
    // accessor exists for the host-side unit test to plant/locate known data.
    GameRoot* root() { return &root_; }

    // ---------------- The "game's public API" ----------------
    // The overlay UI calls these for anything that CHANGES state. This is the
    // same relationship a debug menu has with a game engine: state changes flow
    // through code; introspection only ever reads.
    void SetTimeScale(float v);         // 0.25 .. 4.0
    void SetMoveSpeedMul(float v);      // 0.25 .. 4.0
    void SetVehicleSpeedMul(float v);   // 0.25 .. 4.0
    void SetWorkerEfficiency(float v);  // clamps every worker 0.4 .. 1.0
    void SetAutoSmelt(bool on);
    void SetStaminaBoost(bool on);
    void SetFastHaul(bool on);
    void SetEngineOn(int vehicleIndex, bool on);
    void RefuelVehicle(int vehicleIndex);
    void CollectPendingIncome();        // pendingIncome -> coins
    void RespawnPlayer();               // health/maxHealth, resets position

private:
    DemoGame();
    ~DemoGame();
    void TickLoop();
    void Tick(float dt);   // one simulation step

    GameRoot root_{};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> tickCount_{0};
    double simTime_ = 0.0;    // seconds of simulated time
    // Imperfect pointer kept here on purpose: no mutex. Torn reads between the
    // tick thread and readers are caught by ValidateRoot's checksum instead —
    // see the lock-free note at the top of this file.
};

} // namespace demo
