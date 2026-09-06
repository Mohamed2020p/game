// cheats.cpp — cheat implementations
#include "cheats.h"
#include <android/log.h>

#define LOG_TAG "Cheats"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace cheats {

Settings g_settings;

// ---- Helper: get singleton pointer ----
uintptr_t GetSingleton(pid_t pid, uintptr_t base, uintptr_t offset,
                       const std::vector<mem::MemoryRegion>& maps) {
    uintptr_t ptr = 0;
    if (!mem::SafeReadValue(base + offset, &ptr, maps)) return 0;
    return ptr;
}

// ---- Helper: find MinePlayer instance (pointer chase) ----
uintptr_t FindMinePlayer(pid_t pid, uintptr_t base,
                         const std::vector<mem::MemoryRegion>& maps) {
    // In IL2CPP, singletons are often at fixed offsets.
    // Try to find through MainManager or CCDirector.
    uintptr_t ccdirector = GetSingleton(pid, base, Game::GameSingleton::CCDirector, maps);
    if (ccdirector) {
        // CCDirector often has a reference to the player
        uintptr_t player = 0;
        // Try offset 0x?? — you'll need to find the actual offset
        // For now, this is a placeholder that you'll fill in.
        mem::SafeReadValue(ccdirector + 0x??, &player, maps);
        if (player) return player;
    }
    return 0;
}

// ---- Helper: find CurrencyManager instance ----
uintptr_t FindCurrencyManager(pid_t pid, uintptr_t base,
                              const std::vector<mem::MemoryRegion>& maps) {
    // CurrencyManager is likely a singleton or static class.
    // Try to find it via a known pointer or search.
    // For now, we'll use the DEV function as a fallback.
    // In a real mod, you'd find the actual instance.
    return 0;
}

// ---- Apply individual cheats ----

void ApplyUnlimitedMoney(pid_t pid, uintptr_t base,
                         const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.unlimited_money) return;
    
    // Method 1: Hook CurrencyManager.GetAmount (can't do without injection)
    // Method 2: Write directly to CurrencyInst._amount
    // We need to find the CurrencyManager instance first.
    // This is the trickiest part — you need to find the instance.
    // For now, use the DEV function (which uses the game's own code).
    
    // Since we can't hook without injection, we'll use a different approach:
    // Find the CurrencyInst object in memory and write to _amount.
    // You'll need to find where the CurrencyManager stores its instance.
    
    // This is a placeholder — you'll need to find the actual address.
    // The CurrencyManager is likely a singleton at a fixed offset in the data section.
    // You can find it by searching the game's memory for the CurrencyInst pattern.
    
    // For now, log that it's not implemented yet.
    static bool warned = false;
    if (!warned) {
        LOGI("Unlimited Money: needs CurrencyManager instance finder");
        warned = true;
    }
}

void ApplyFreeUpgrades(pid_t pid, uintptr_t base,
                       const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.free_upgrades) return;
    // Hook CurrencyManager.TrySpend — needs injection
    // Without injection, we can't hook.
    // As a fallback, we could write to the currency amount directly.
    LOGI("Free Upgrades: needs hooking TrySpend (injection required)");
}

void ApplyMaxSpeed(pid_t pid, uintptr_t base,
                   const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.max_speed) return;
    // Find MinePlayer instance and write to move speed
    uintptr_t player = FindMinePlayer(pid, base, maps);
    if (player) {
        // Write speed value to the player's speed field
        // You need to find the actual speed field offset.
        // This is a placeholder offset — you'll need to find it.
        mem::WriteMemoryValue(player + 0x??, g_settings.speed_value, maps);
    }
}

void ApplyOneHitKill(pid_t pid, uintptr_t base,
                     const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.one_hit_kill) return;
    // This modifies damage output — needs injection to hook HitBox.DamageExecute
    // Without injection, we can't intercept the damage call.
    // Alternative: write to the damage calculation stat (e.g., attack power multiplier)
    LOGI("One-Hit Kill: needs hooking DamageExecute (injection required)");
}

void ApplyInstantMining(pid_t pid, uintptr_t base,
                        const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.instant_mining) return;
    // Modify attack interval — needs injection to hook GetAttackInterval
    LOGI("Instant Mining: needs hooking GetAttackInterval (injection required)");
}

void ApplyMaxCargo(pid_t pid, uintptr_t base,
                   const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.max_cargo) return;
    // Write to the cargo stat or hook GetMaxWeight
    LOGI("Max Cargo: needs hooking GetMaxWeight (injection required)");
}

void ApplyCriticalChance(pid_t pid, uintptr_t base,
                         const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.critical_chance) return;
    // Hook GetCriticalChance to return 100
    LOGI("Critical Chance: needs hooking GetCriticalChance (injection required)");
}

void ApplyVehicleSpeed(pid_t pid, uintptr_t base,
                       const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.vehicle_speed) return;
    // Hook GetVehicleMoveSpeed
    LOGI("Vehicle Speed: needs hooking GetVehicleMoveSpeed (injection required)");
}

void ApplyUnlimitedFuel(pid_t pid, uintptr_t base,
                        const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.unlimited_fuel) return;
    // Hook GetFuelAmount or write to fuel field
    LOGI("Unlimited Fuel: needs hooking GetFuelAmount (injection required)");
}

void ApplyVehicleDamage(pid_t pid, uintptr_t base,
                        const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.vehicle_damage) return;
    // Hook GetVehicleAttackPower
    LOGI("Vehicle Damage: needs hooking GetVehicleAttackPower (injection required)");
}

void ApplyVehicleCargo(pid_t pid, uintptr_t base,
                       const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.vehicle_cargo) return;
    // Hook GetVehicleCargoStat
    LOGI("Vehicle Cargo: needs hooking GetVehicleCargoStat (injection required)");
}

void ApplyOneHitBreak(pid_t pid, uintptr_t base,
                      const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.one_hit_break) return;
    // Hook MineBlockInst.TakeDamage to set HP to 0
    LOGI("One-Hit Break: needs hooking MineBlockInst.TakeDamage (injection required)");
}

void ApplyMaxOreDrop(pid_t pid, uintptr_t base,
                     const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.max_ore_drop) return;
    // Hook GetDropStageIndex to return max
    LOGI("Max Ore Drop: needs hooking GetDropStageIndex (injection required)");
}

void ApplyAutoDigSpeed(pid_t pid, uintptr_t base,
                       const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.auto_dig_speed) return;
    // Override AutoDiggerActor.DIG_INTERVAL constant (needs injection)
    LOGI("Auto-Dig Speed: needs patching DIG_INTERVAL (injection required)");
}

void ApplyUnlimitedOil(pid_t pid, uintptr_t base,
                       const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.unlimited_oil) return;
    // Hook or write to oil generation
    LOGI("Unlimited Oil: needs hooking oil system (injection required)");
}

void ApplyWorkerSpeed(pid_t pid, uintptr_t base,
                      const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.worker_speed) return;
    // Write to worker speed stat or hook it
    LOGI("Worker Speed: needs hooking worker speed stats (injection required)");
}

void ApplyWorkerStamina(pid_t pid, uintptr_t base,
                        const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.worker_stamina) return;
    LOGI("Worker Stamina: needs hooking worker stamina (injection required)");
}

void ApplyWorkerAttack(pid_t pid, uintptr_t base,
                       const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.worker_attack) return;
    LOGI("Worker Attack: needs hooking worker attack (injection required)");
}

void ApplyWorkerCargo(pid_t pid, uintptr_t base,
                      const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.worker_cargo) return;
    LOGI("Worker Cargo: needs hooking worker cargo (injection required)");
}

void ApplyInstantSmelt(pid_t pid, uintptr_t base,
                       const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.instant_smelt) return;
    LOGI("Instant Smelt: needs hooking smelting speed (injection required)");
}

void ApplyMaxCriticalSmelt(pid_t pid, uintptr_t base,
                           const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.max_critical_smelt) return;
    LOGI("Max Critical Smelt: needs hooking critical smelt (injection required)");
}

void ApplyMaxSalesPrice(pid_t pid, uintptr_t base,
                        const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.max_sales_price) return;
    LOGI("Max Sales Price: needs hooking sales price (injection required)");
}

void ApplyGodMode(pid_t pid, uintptr_t base,
                  const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.god_mode) return;
    // Prevent damage to player — needs hooking damage receiver
    LOGI("God Mode: needs hooking player damage (injection required)");
}

void ApplyUnlockAll(pid_t pid, uintptr_t base,
                    const std::vector<mem::MemoryRegion>& maps) {
    if (!g_settings.unlock_all) return;
    // Write to EquipmentInst.IsPurchased/IsUnlocked
    LOGI("Unlock All: needs writing to equipment flags (injection required)");
}

// ---- Apply all active cheats ----
void ApplyAll(pid_t target_pid, uintptr_t il2cpp_base,
              const std::vector<mem::MemoryRegion>& maps) {
    if (target_pid <= 0 || il2cpp_base == 0) return;
    
    ApplyUnlimitedMoney(target_pid, il2cpp_base, maps);
    ApplyFreeUpgrades(target_pid, il2cpp_base, maps);
    ApplyMaxSpeed(target_pid, il2cpp_base, maps);
    ApplyOneHitKill(target_pid, il2cpp_base, maps);
    ApplyInstantMining(target_pid, il2cpp_base, maps);
    ApplyMaxCargo(target_pid, il2cpp_base, maps);
    ApplyCriticalChance(target_pid, il2cpp_base, maps);
    ApplyVehicleSpeed(target_pid, il2cpp_base, maps);
    ApplyUnlimitedFuel(target_pid, il2cpp_base, maps);
    ApplyVehicleDamage(target_pid, il2cpp_base, maps);
    ApplyVehicleCargo(target_pid, il2cpp_base, maps);
    ApplyOneHitBreak(target_pid, il2cpp_base, maps);
    ApplyMaxOreDrop(target_pid, il2cpp_base, maps);
    ApplyAutoDigSpeed(target_pid, il2cpp_base, maps);
    ApplyUnlimitedOil(target_pid, il2cpp_base, maps);
    ApplyWorkerSpeed(target_pid, il2cpp_base, maps);
    ApplyWorkerStamina(target_pid, il2cpp_base, maps);
    ApplyWorkerAttack(target_pid, il2cpp_base, maps);
    ApplyWorkerCargo(target_pid, il2cpp_base, maps);
    ApplyInstantSmelt(target_pid, il2cpp_base, maps);
    ApplyMaxCriticalSmelt(target_pid, il2cpp_base, maps);
    ApplyMaxSalesPrice(target_pid, il2cpp_base, maps);
    ApplyGodMode(target_pid, il2cpp_base, maps);
    ApplyUnlockAll(target_pid, il2cpp_base, maps);
}

} // namespace cheats
