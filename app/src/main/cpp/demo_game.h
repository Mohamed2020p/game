// =============================================================================
//  game.h — Real game structures from dump.cs
// =============================================================================
//
//  All addresses are RVAs in libil2cpp.so.
//  All field offsets are from the IL2CPP class layouts.
//  Source: dump.cs (Il2CppDumper output)
//
//  DO NOT use this on any game you don't own.
//  For educational research only.
// =============================================================================

#pragma once

#include <cstdint>
#include <cstddef>

namespace Game {

// --- Base address (set at runtime from /proc/self/maps) ---
extern uintptr_t il2cpp_base;
extern pid_t game_pid;

// =============================================================================
//  PLAYER SYSTEM — MinePlayer (TypeDefIndex 1040)
// =============================================================================

class MinePlayer {
public:
    // ---- Function RVAs (from dump.cs) ----
    static constexpr uintptr_t GetMoveSpeed = 0xD9B8F4;
    static constexpr uintptr_t UpdateMoveSpeed = 0xDA0EA0;
    static constexpr uintptr_t GetAttackPowerBase = 0xD9B8D0;
    static constexpr uintptr_t GetAttackPowerMultiplier = 0xD9B8DC;
    static constexpr uintptr_t GetAttackInterval = 0xD9B8C4;
    static constexpr uintptr_t GetCriticalChance = 0xD9B8E8;
    static constexpr uintptr_t GetAbsorbRange = 0xD9B900;
    static constexpr uintptr_t Attack = 0xDA10E0;
    static constexpr uintptr_t CalcAdditionalDamage = 0xDA1458;
    static constexpr uintptr_t CalcAdditionalCritical = 0xDA14C8;
    static constexpr uintptr_t SwitchMiningTool = 0xD9EEF0;
    static constexpr uintptr_t SwitchCarryingTool = 0xD9F038;
    static constexpr uintptr_t GetWeight = 0xD9B4D4;
    static constexpr uintptr_t GetMaxWeight = 0xD9B620;
    static constexpr uintptr_t BeginDynamiteAim = 0xD9F82C;
    static constexpr uintptr_t ReleaseDynamiteThrow = 0xDA0114;
    static constexpr uintptr_t OnGetOnVehicle = 0xDA0424;
    static constexpr uintptr_t OnGetOffVehicle = 0xDA071C;
    static constexpr uintptr_t LookAttackTarget = 0xDA12E8;
    static constexpr uintptr_t OnStatChanged = 0xDA0E58;
};

// =============================================================================
//  STAT HELPER — MineUnitStatHelper (TypeDefIndex 785)
// =============================================================================

class StatHelper {
public:
    static constexpr uintptr_t GetCharacterMoveSpeed = 0xD33660;
    static constexpr uintptr_t GetVehicleMoveSpeed = 0xD33BE8;
    static constexpr uintptr_t GetCharacterAttackPower = 0xD32A4C;
    static constexpr uintptr_t GetVehicleAttackPower = 0xD32D98;
    static constexpr uintptr_t GetCharacterAttackInterval = 0xD32E88;
    static constexpr uintptr_t GetVehicleAttackInterval = 0xD3338C;
    static constexpr uintptr_t GetCharacterCargoStat = 0xD31AD0;
    static constexpr uintptr_t GetVehicleCargoStat = 0xD31FD4;
    static constexpr uintptr_t GetMiningCriticalChance = 0xD32DCC;
    static constexpr uintptr_t GetCharacterAbsorbRange = 0xD34058;
    static constexpr uintptr_t GetVehicleAbsorbRange = 0xD3455C;
    static constexpr uintptr_t GetFuelAmount = 0xD34830;
    static constexpr uintptr_t GetFuelEfficiencyMulti = 0xD34A84;
    static constexpr uintptr_t GetStaminaAmount = 0xD34CD8;
    static constexpr uintptr_t GetStaminaEfficiency = 0xD34F2C;
    static constexpr uintptr_t GetStaminaRecoverTime = 0xD35080;
};

// =============================================================================
//  DAMAGE SYSTEM — HitBox / HitBoxPool
// =============================================================================

class HitBox {
public:
    // ---- Field offsets (inside object) ----
    static constexpr uintptr_t _baseDamage = 0x30;
    static constexpr uintptr_t _lifeTime = 0x14;
    static constexpr uintptr_t _maxTargetCount = 0x18;
    static constexpr uintptr_t _hitTargets = 0x38;
    
    // ---- Function RVAs ----
    static constexpr uintptr_t DamageExecute = 0xDCC20C;
    static constexpr uintptr_t OnTriggerEnter = 0xDCC0E8;
    static constexpr uintptr_t ResetHitBox = 0xDCBD2C;
    
    static constexpr float CRITICAL_MULTIPLIER = 2.0f;
};

class HitBoxPool {
public:
    static constexpr uintptr_t CreateHitBox = 0xDCCBD4;
};

class DamageInfo {
public:
    // ---- Field offsets ----
    static constexpr uintptr_t Caster = 0x0;
    static constexpr uintptr_t Target = 0x4;
    static constexpr uintptr_t HitBoxType = 0x8;
    static constexpr uintptr_t EffectScale = 0xC;
    static constexpr uintptr_t DamageAmount = 0x10;   // ⭐ the hit value
    static constexpr uintptr_t HitDir = 0x14;
};

class PickaxeModule {
public:
    // ---- Field offsets ----
    static constexpr uintptr_t _detectRange = 0x14;
    static constexpr uintptr_t _attackInfos = 0x18;
    static constexpr uintptr_t _toolType = 0x1C;
    static constexpr uintptr_t _hitBoxType = 0x24;
    static constexpr uintptr_t _attackInterval = 0x3C;
    static constexpr uintptr_t _lastAttackTime = 0x34;
    
    // ---- Function RVAs ----
    static constexpr uintptr_t Update = 0xDB0B00;
    static constexpr uintptr_t StartAttack = 0xDB1220;
    static constexpr uintptr_t PlayAttackAnimation = 0xDB1870;
};

struct AttackInfo {
    static constexpr uintptr_t toolType = 0x8;
    static constexpr uintptr_t hitTimeFrame = 0xC;
    static constexpr uintptr_t animationDurationFrame = 0x10;
    static constexpr uintptr_t isSync = 0x14;
    static constexpr uintptr_t hitboxType = 0x18;
};

// =============================================================================
//  ORE BLOCK SYSTEM — MineBlockInst
// =============================================================================

class MineBlockInst {
public:
    // ---- Field offsets ----
    static constexpr uintptr_t _oreType = 0x8;
    static constexpr uintptr_t _oreDef = 0xC;
    static constexpr uintptr_t _hp = 0x24;        // ⭐ current HP
    static constexpr uintptr_t _maxHP = 0x28;     // ⭐ max HP
    
    // ---- Function RVAs ----
    static constexpr uintptr_t TakeDamage = 0xD29934;
    static constexpr uintptr_t TakeSelfDamage = 0xD29E94;
    static constexpr uintptr_t HPProgress = 0xD297C0;
    static constexpr uintptr_t LoadCompact = 0xD29FBC;
    static constexpr uintptr_t GetDropStageIndex = 0xD29C20;
};

class BreakableObject {
public:
    static constexpr uintptr_t _duration = 0x20;
    static constexpr uintptr_t _disappearDelay = 0x1C;
};

// =============================================================================
//  CURRENCY / MONEY SYSTEM — CurrencyManager / CurrencyInst
// =============================================================================

class CurrencyManager {
public:
    // ---- Function RVAs ----
    static constexpr uintptr_t GetAmount = 0xE4ACA0;              // ⭐ read money
    static constexpr uintptr_t SetAmount = 0xE4AE00;              // ⭐ write money
    static constexpr uintptr_t Obtain = 0xE4B190;
    static constexpr uintptr_t Obtain_World = 0xE4B9E8;
    static constexpr uintptr_t Obtain_UI = 0xE4C290;
    static constexpr uintptr_t Obtain_WithoutFX = 0xE4CB34;
    static constexpr uintptr_t CheckSpendable = 0xE4CED4;
    static constexpr uintptr_t Spend = 0xE4D09C;
    static constexpr uintptr_t TrySpend = 0xE4D43C;               // ⭐ free upgrades
    static constexpr uintptr_t GetAccumulateAmount = 0xE4AD50;
    static constexpr uintptr_t DEV_ShowMeTheMoreMoney = 0xE4DE88;
    static constexpr uintptr_t DEV_ResetAmount = 0xE4E3C0;
};

class CurrencyInst {
public:
    // ---- Field offsets ----
    static constexpr uintptr_t _currencyType = 0x8;
    static constexpr uintptr_t _prevAmount = 0xC;
    static constexpr uintptr_t _amount = 0x10;                   // ⭐ current balance
    static constexpr uintptr_t _accumulatedAmount = 0x18;
    static constexpr uintptr_t _isDiscovered = 0x20;
};

// =============================================================================
//  VEHICLE SYSTEM — VehicleBase
// =============================================================================

class VehicleBase {
public:
    // ---- Constants ----
    static constexpr float OIL_PER_ITEM = 5.0f;
    static constexpr float SPEED_MULT_WHEN_OIL_EMPTY = 0.05f;
    
    // ---- Function RVAs ----
    static constexpr uintptr_t DealDamageToTarget = 0xDB49F4;
    static constexpr uintptr_t GetAttackPowerBase = 0xDB49B8;
    static constexpr uintptr_t GetAttackPowerMultiplier = 0xDB49C4;
    static constexpr uintptr_t AbsorbOilFromUnit = 0xDB45C4;
    static constexpr uintptr_t TurnOnVehicle = 0xDB3B48;
    static constexpr uintptr_t TurnOffVehicle = 0xDB3F20;
    
    // ---- Field offsets ----
    static constexpr uintptr_t _oilAbsorber = 0x44;
    static constexpr uintptr_t _cameraTarget = 0x50;
    static constexpr uintptr_t _rearLength = 0x54;
};

class MineUnitBase {
public:
    // ---- Field offsets ----
    static constexpr uintptr_t _rigid = 0x10;
    static constexpr uintptr_t _col_hit = 0x14;
    static constexpr uintptr_t _agent = 0x18;
    static constexpr uintptr_t _rotator = 0x1C;
    static constexpr uintptr_t _stackingSender = 0x20;
    static constexpr uintptr_t _stackingReciever = 0x24;
    static constexpr uintptr_t _mover = 0x34;
};

// =============================================================================
//  MOVEMENT SYSTEM — MineUnitMover
// =============================================================================

class MineUnitMover {
public:
    static constexpr uintptr_t SetMoveSpeed = 0xDA6D74;
    static constexpr uintptr_t Move = 0xDA42FC;
    static constexpr uintptr_t PathMove = 0xDA4580;
    static constexpr uintptr_t PathMoveImmediately = 0xDA4844;
    static constexpr uintptr_t ForceMove = 0xDA41A8;
    static constexpr uintptr_t Stop = 0xDA17B4;
    static constexpr uintptr_t CanMove = 0xDA6D7C;
    static constexpr uintptr_t CanReachTarget = 0xDA165C;
};

// =============================================================================
//  DYNAMITE SYSTEM — DynamiteBehaviour
// =============================================================================

class DynamiteBehaviour {
public:
    // ---- Field offsets ----
    static constexpr uintptr_t _radius = 0x20;
    static constexpr uintptr_t _explosionDuration = 0x24;
    static constexpr uintptr_t _throwFlightTime = 0x28;
    static constexpr uintptr_t _throwArcHeight = 0x2C;
    static constexpr uintptr_t _throwPower = 0x34;
    
    // ---- Function RVAs ----
    static constexpr uintptr_t LaunchArc = 0xDCEEE4;
    static constexpr uintptr_t Co_Explode = 0xDCF4D4;
    static constexpr uintptr_t TryGetBallisticVelocity = 0xDCF63C;
};

// =============================================================================
//  WORKER SYSTEM — MineWorker
// =============================================================================

class MineWorker {
public:
    // ---- TypeDefIndex ----
    static constexpr int TypeDefIndex = 1072;
    
    // ---- Field offsets ----
    static constexpr uintptr_t PickaxeModule = 0x40;
    
    // ---- Stat IDs ----
    static constexpr int WorkerMoveSpeed = 1020101;        // Base
    static constexpr int WorkerStaminaAmount = 1030101;
    static constexpr int WorkerStaminaEfficiency = 1040101;
    static constexpr int WorkerStaminaRecoverTime = 1050101;
    static constexpr int WorkerAttackPower = 1060101;
    static constexpr int WorkerCargo = 1070101;
};

// =============================================================================
//  STRUCTURE SYSTEM — AutoDigger / OilStation
// =============================================================================

class AutoDiggerActor {
public:
    static constexpr float DIG_INTERVAL = 1.0f;
    static constexpr float DIG_START_ANIM_DURATION = 1.25f;
};

class OilStationInst {
public:
    static constexpr int MAX_WEIGHT = 9;
};

enum class EStructureType : int32_t {
    Bridge = 1,
    Minecart = 2,
    AutoDigger = 3,
    AutoHarvester = 4,
    ExtraOilStation = 5
};

enum class EStructurePayType : int32_t {
    Free = 1,
    Item = 2,
    RV = 3
};

// =============================================================================
//  SMELTER & SALES SYSTEM
// =============================================================================

class SmelterSystem {
public:
    // ---- Stat IDs ----
    static constexpr int SmelterSmeltingSpeedBase = 4010101;
    static constexpr int SmeltingCriticalChanceBase = 4010501;
    static constexpr int GoldExtraSalesBase = 5000205;
    
    // ---- Function RVAs ----
    static constexpr uintptr_t GetRefiningTime = 0xD6E298;
};

// =============================================================================
//  EQUIPMENT SYSTEM
// =============================================================================

class EquipmentInst {
public:
    static constexpr uintptr_t IsPurchased = 0x10;
    static constexpr uintptr_t IsUnlocked = 0x11;
    static constexpr uintptr_t PurchaseCount = 0x14;
};

class EquipmentManager {
public:
    static constexpr uintptr_t GetWorkerEquipmentIDByIndex = 0xE10550;
};

// =============================================================================
//  SINGLETON POINTERS (from dump.cs)
// =============================================================================

// These are pointers to singletons inside the game's memory.
// At runtime: read from libil2cpp.so + these offsets.
class GameSingleton {
public:
    static constexpr uintptr_t CCDirector = 0x4f06288;
    static constexpr uintptr_t UserInfo = 0x4e9feb8;
    static constexpr uintptr_t MainManager = 0x4dde3e0;
    static constexpr uintptr_t MenuManager = 0x4dfe838;
};

// =============================================================================
//  STAT ID REFERENCE (EStatType)
// =============================================================================

enum class EStatType : int32_t {
    // Character
    CharacterMoveSpeedMulti = 1000102,
    PlayerMoveSpeedBase = 1010101,
    PlayerMoveSpeedMulti = 1010102,
    PlayerMoveSpeedFinal = 1010103,
    WorkerMoveSpeedBase = 1020101,
    WorkerMoveSpeedMulti = 1020102,
    WorkerMoveSpeedFinal = 1020103,
    WorkerStaminaAmountBase = 1030101,
    WorkerStaminaEfficiencyBase = 1040101,
    WorkerStaminaRecoverTimeBase = 1050101,
    WorkerAttackPowerBase = 1060101,
    WorkerCargoBase = 1070101,
    
    // Hand Tools
    HandToolAttackPowerMulti = 2000102,
    HandToolAttackIntervalMulti = 2000202,
    HandToolCargoMulti = 2000302,
    HandToolAbsorbRangeMulti = 2000402,
    PickaxeAttackPowerBase = 2010101,
    PickaxeAttackIntervalBase = 2010201,
    SledgeHammerAttackPowerBase = 2020101,
    SledgeHammerAttackIntervalBase = 2020201,
    JackhammerAttackPowerBase = 2030101,
    JackhammerAttackIntervalBase = 2030201,
    LargeHandDrillAttackPowerBase = 2040101,
    LargeHandDrillAttackIntervalBase = 2040201,
    
    // Vehicles
    VehicleMoveSpeedMulti = 3000102,
    VehicleFuelAmountBase = 3000201,
    VehicleFuelEfficiencyMulti = 3000302,
    VehicleFuelPerOilItemBase = 3000401,
    VehicleEmptyFuelMoveSpeedMulti = 3000501,
    VehicleAttackIntervalMulti = 3000602,
    VehicleCargoMulti = 3000702,
    VehicleAbsorbRangeMulti = 3000802,
    HydraulicBreakerMoveSpeedBase = 3010101,
    RollerCrusherMoveSpeedBase = 3020101,
    DrillCrusherMoveSpeedBase = 3030101,
    DumpTruckMoveSpeedBase = 3050101,
    HaulTruckMoveSpeedBase = 3070101,
    WheelLoaderMoveSpeedBase = 3080101,
    BucketWheelMoveSpeedBase = 3090101,
    FuelTruckMoveSpeedBase = 3100101,
    ImpactHaulerMoveSpeedBase = 3110101,
    
    // Smelter / Sales
    SmelterSmeltingSpeedBase = 4010101,
    SmelterSmeltingSpeedMulti = 4010102,
    SmelterSmeltingSpeedFinal = 4010103,
    SmeltingCriticalChanceBase = 4010501,
    SmeltingCriticalChanceFinal = 4010503,
    
    // Ore sales
    GoldExtraSalesBase = 5000205,
};

// =============================================================================
//  ENUMS
// =============================================================================

enum class EHitBoxType : int32_t {
    Pickaxe_Hit = 1,
    SledgeHammer_Hit = 2,
    Jackhammer_Hit = 3,
    LargeHandDrill_Hit = 4,
};

enum class EOreType : int32_t {
    Limestone = 101, Coal = 102, Sandstone = 103, Dunite = 104,
    Granite = 105, Basalt = 106, Yooperlite = 107, Obsidian = 108,
    Bronzite = 109, Ice = 110,
    Copper = 201, Iron = 202, Manganese = 203, Silver = 204,
    Gold = 205, Cobalt = 206, Bismuth = 207, LapisLazuli = 208,
    Quartz = 301, Amber = 302, Amethyst = 303, Malachite = 304,
    Aquamarine = 305, Celestite = 306, Tanzanite = 307, Zircon = 308,
    Opal = 309, Emerald = 310, Ruby = 311, Diamond = 312,
};

enum class ECurrency : int32_t {
    Money = 1,
};

enum class ECurrencyObtain : int32_t {
    SellOrePiece = 100,
};

} // namespace Game
