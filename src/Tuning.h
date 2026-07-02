#pragma once

// Every gameplay constant in one header so feel iteration is edit -> ninja ->
// R-key. Times are seconds unless stated; needs advance in day-fraction units
// so shrinking kSecondsPerDay (headless tests) preserves behavior.
namespace tune {

// --- day/night ---
inline constexpr float kSecondsPerDay = 360.0f;  // 6 real minutes
inline constexpr float kDawnT = 0.24f;           // day fraction villagers rise
inline constexpr float kDuskT = 0.76f;           // start heading home
inline constexpr float kNightT = 0.80f;          // hard bedtime (interrupts)

// --- needs ---
inline constexpr float kHungerPerDay = 1.15f;    // 0 -> 1 in under a day
inline constexpr float kHungerUrgent = 0.85f;    // drop everything and eat
inline constexpr float kHungerWant = 0.62f;      // eat at next task boundary
inline constexpr float kHungerAfterMeal = 0.05f;
inline constexpr float kEnergyDrainPerDay = 0.85f;
inline constexpr float kEnergyRestorePerDay = 3.2f;  // while sleeping

// --- villagers ---
inline constexpr int kStartPopulation = 8;
inline constexpr int kMaxPopulation = 24;
inline constexpr float kWalkSpeed = 3.4f;
inline constexpr float kPanicSpeedFactor = 1.55f;
inline constexpr float kCarrySpeedFactor = 0.8f;
inline constexpr float kSwimSpeed = 1.4f;
inline constexpr float kTurnRate = 6.5f;         // rad/s
inline constexpr float kVillagerRadius = 0.55f;  // pick/physics sphere
inline constexpr float kThinkInterval = 0.4f;    // decision cadence
inline constexpr float kChildScale = 0.55f;
inline constexpr float kChildGrowDays = 2.0f;
inline constexpr float kFearDecayPerSec = 0.04f;

// --- jobs & economy (wood in logs, food in meals) ---
inline constexpr float kWorkRadius = 120.0f;     // from village center
inline constexpr int kLogsPerTree = 2;
inline constexpr float kChopSwingSeconds = 0.9f;
inline constexpr int kChopSwings = 8;
inline constexpr float kPickupSeconds = 0.5f;
inline constexpr float kDepositSeconds = 0.4f;
inline constexpr int kHouseWoodCost = 8;
inline constexpr float kHouseBuildSeconds = 30.0f;  // hammering, all stages
inline constexpr float kTendSeconds = 6.0f;
inline constexpr float kHarvestSeconds = 1.6f;
inline constexpr float kCropGrowPerDay = 0.85f;
inline constexpr float kCropTendBoost = 3.0f;    // growth multiplier after tending
inline constexpr float kCropTendWindow = 30.0f;
inline constexpr int kFoodPerHarvest = 4;
inline constexpr int kFoodPerCatch = 3;
inline constexpr float kCastSeconds = 4.5f;
inline constexpr float kCatchChance = 0.4f;      // per cast
inline constexpr int kCatchesPerTrip = 2;
inline constexpr float kEatSeconds = 3.0f;
inline constexpr int kStartFood = 25;
inline constexpr int kStartWood = 4;
inline constexpr float kGrowthFoodPerCapita = 1.5f;  // surplus needed for a child

// --- hand <-> villager ---
inline constexpr float kPlaceSpeed = 7.0f;   // release below this = gentle place
inline constexpr float kStunSpeed = 10.0f;   // landing impact above this = stunned
inline constexpr float kMaxThrowSpeed = 70.0f;
inline constexpr float kPanicSeconds = 4.0f;
inline constexpr float kReactRadiusPerAltitude = 1.5f;  // hand low = scarier

// --- mortality seam (this slice: invulnerable) ---
inline constexpr bool kVillagersInvulnerable = true;
inline constexpr float kLethalImpactSpeed = 26.0f;  // unused while invulnerable
inline constexpr float kVillagerRestitution = 0.05f;

}  // namespace tune
