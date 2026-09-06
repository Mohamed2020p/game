
// cheats.h — all cheat toggles
#pragma once

#include <cstdint>
#include <vector>
#include "game.h"
#include "memory.h"

namespace cheats {

// ---- Settings struct (saved to JSON) ----
struct Settings {
    // Player
    bool unlimited_money = false;
    bool free_upgrades = false;
    bool max_speed = false;
    float speed_value = 999.0f;
    bool one_hit_kill = false;
    bool instant_mining = false;
    bool max_cargo = false;
    bool critical_chance = false;
    int damage_multiplier = 100;

    // Vehicle
    bool vehicle_speed = false;
    bool unlimited_fuel = false;
    bool vehicle_damage = false;
    bool vehicle_one_hit = false;
    bool vehicle_cargo = false;

    // Ore
    bool one_hit_break = false;
    bool max_ore_drop = false;
    bool auto_dig_speed = false;
    bool unlimited_oil = false;

    // Worker
    bool worker_speed = false;
    bool worker_stamina = false;
    bool worker_attack = false;
    bool worker_cargo = false;

    // Economy
    bool instant_smelt = false;
    bool max_critical_smelt = false;
    bool max_sales_price = false;

    // Misc
    bool god_mode = false;
    bool unlock_all = false;
    bool free_iap = false;
};

extern Settings g_settings;

// ---- Apply all active cheats (call each frame) ----
void ApplyAll(pid_t target_pid, uintptr_t il2cpp_base,
              const std::vector<mem::MemoryRegion>& maps);

// ---- Individual cheat functions ----
void ApplyUnlimitedMoney(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyFreeUpgrades(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyMaxSpeed(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyOneHitKill(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyInstantMining(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyMaxCargo(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyCriticalChance(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyVehicleSpeed(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyUnlimitedFuel(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyVehicleDamage(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyVehicleCargo(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyOneHitBreak(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyMaxOreDrop(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyAutoDigSpeed(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyUnlimitedOil(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyWorkerSpeed(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyWorkerStamina(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyWorkerAttack(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyWorkerCargo(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyInstantSmelt(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyMaxCriticalSmelt(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyMaxSalesPrice(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyGodMode(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);
void ApplyUnlockAll(pid_t pid, uintptr_t base, const std::vector<mem::MemoryRegion>& maps);

} // namespace cheats
