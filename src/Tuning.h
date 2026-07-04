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

// --- scaffolds & buildings ---
inline constexpr int kScaffoldWoodCost = 4;
inline constexpr float kScaffoldCraftSeconds = 8.0f;
inline constexpr int kMaxLooseScaffolds = 3;   // auto-crafting stops here
inline constexpr int kMaxScaffoldStack = 7;
inline constexpr float kScaffoldCombineRadius = 1.8f;
inline constexpr float kBuildSecondsPerScaffold = 12.0f;
inline constexpr float kBuildPlacementRange = 55.0f;  // from the village center
inline constexpr int kBedsSmallAbode = 4;
inline constexpr int kBedsLargeAbode = 8;
inline constexpr int kBaseFoodCap = 60;
inline constexpr int kBaseWoodCap = 30;
inline constexpr int kStoreFoodCap = 80;   // added per completed Store
inline constexpr int kStoreWoodCap = 50;
inline constexpr float kCrecheSurplusFactor = 0.6f;  // eases the birth check
inline constexpr float kGraveyardBeliefPerDay = 0.015f;  // symbolic until M2
inline constexpr int kDispenserMaxCharges = 3;
inline constexpr float kDispenserCastRadius = 25.0f;
inline constexpr float kWonderAuraRadius = 45.0f;
inline constexpr float kWonderDecayFactor = 0.5f;   // belief decay inside aura
inline constexpr float kWonderAweFactor = 1.5f;
inline constexpr float kCenterInfluencePerLevel = 8.0f;
inline constexpr float kCenterManaPerLevel = 0.15f;  // worship multiplier/level

// --- the map's villages ---
inline constexpr int kNeutralVillages = 2;       // target; fewer if the island is hostile
inline constexpr float kVillageMinSeparation = 130.0f;
inline constexpr float kNeutralBeliefStart = 0.05f;
inline constexpr float kNeutralBeliefFloor = 0.02f;

// --- gods & conversion (the ratchet) ---
inline constexpr int kMaxGods = 2;               // the player + one rival (M5)
inline constexpr float kConvertNeutralBelief = 0.5f;  // neutral joins a god here...
inline constexpr float kConvertLeadMargin = 0.1f;     // ...if clearly ahead of rivals
inline constexpr float kStealBelief = 0.85f;     // stealing an OWNED village needs
inline constexpr float kStealOwnerBelow = 0.35f; // overwhelming faith + a lapsed owner
inline constexpr float kConversionScatter = 0.35f;  // fraction who panic at the flip
inline constexpr float kAweGiftThrown = 0.04f;   // a gift hurled from afar, received

// --- worship, belief, mana, miracles ---
inline constexpr float kManaStart = 20.0f;
inline constexpr float kManaMax = 100.0f;
inline constexpr float kManaPerWorshipperPerDay = 60.0f;  // scaled by belief
inline constexpr float kWorshipHungerFactor = 1.6f;   // dancing is hard work
inline constexpr float kWorshipEnergyFactor = 1.8f;
inline constexpr float kWorshipDanceRadius = 3.6f;    // ring around the totem
inline constexpr float kWorshipDanceRate = 0.35f;     // rad/s circling speed
inline constexpr float kBeliefStart = 0.25f;
inline constexpr float kBeliefFloor = 0.12f;
inline constexpr float kBeliefDecayPerDay = 0.08f;
inline constexpr float kBeliefFromWorshipPerDay = 0.06f;  // per active dancer
inline constexpr float kAweGrab = 0.004f;
inline constexpr float kAweThrow = 0.012f;
inline constexpr float kAweGift = 0.06f;  // food into the stores wins hearts
inline constexpr float kAweMiracle = 0.09f;
inline constexpr float kTempleInfluence = 70.0f;      // base ring radius
inline constexpr float kVillageInfluenceBase = 50.0f;
inline constexpr float kVillageInfluenceScale = 90.0f;  // + belief * this
inline constexpr float kFoodMiracleCost = 30.0f;
inline constexpr int kFoodMiracleBundles = 4;

// --- the rival god (M5/M8): difficulty is cadence and reach, never cheats.
// EASY/FAIR/CRUEL differ only in tempo, appetite, and boldness; the same
// brain plays all three. Index = GodAI::profile (menu default FAIR).
struct AiProfile {
  float thinkPeriod;   // strategos decision cadence (s)
  float actCooldown;   // pause between hand orders (s)
  float handSpeed;     // m/s enemy hand travel
  float aggression;    // chance a spare gift targets YOU
  float foodReserve;   // feed an owned village below this
  float manaReserve;   // keep this much after courting casts
};
inline constexpr AiProfile kAiProfiles[3] = {
    {5.0f, 4.5f, 20.0f, 0.15f, 14.0f, 30.0f},  // EASY: ponderous, timid
    {3.0f, 2.5f, 26.0f, 0.35f, 10.0f, 15.0f},  // FAIR: the honest baseline
    {1.6f, 1.0f, 32.0f, 0.60f, 8.0f, 8.0f},    // CRUEL: relentless
};
inline constexpr const char* kAiProfileNames[3] = {"EASY", "FAIR", "CRUEL"};
inline constexpr float kAiHandHover = 5.5f;      // carry height above ground
inline constexpr float kAiThrowRange = 110.0f;   // gift runs launch from here
inline constexpr int kAiWorshippersPer = 8;      // +1 worshipper per N villagers

// --- the map editor (M6) ---
inline constexpr int kEditorPresetPop[3] = {5, 8, 12};    // small/medium/large
inline constexpr int kEditorPresetFood[3] = {15, 25, 40};
inline constexpr int kEditorPresetWood[3] = {2, 4, 8};
inline constexpr float kEditorVillageSeparation = 60.0f;  // author freedom > 130
inline constexpr float kEditorBrushMin = 4.0f;
inline constexpr float kEditorBrushMax = 60.0f;
inline constexpr float kEditorRaiseRate = 9.0f;    // m/s at full strength
inline constexpr float kEditorFlattenRate = 4.0f;  // strength/s toward anchor
inline constexpr float kEditorSmoothRate = 5.0f;
inline constexpr float kEditorPaintPeriod = 0.12f; // s between plant ticks

// --- mortality (M2: the seams are live) ---
inline constexpr bool kVillagersInvulnerable = false;
inline constexpr float kLethalImpactSpeed = 26.0f;  // impact above this kills
inline constexpr float kDrownSeconds = 16.0f;       // swimming this long kills
inline constexpr float kStarveDays = 1.5f;          // at hunger 1.0, unfed
inline constexpr float kBurySeconds = 3.0f;
inline constexpr float kBurialBelief = 0.02f;       // dignity restores faith
inline constexpr float kBeliefDeathPenalty = 0.02f; // every death shakes it
inline constexpr float kCorpseRotDays = 1.0f;       // unburied after this: rot
inline constexpr float kCorpseBeliefPerDay = 0.06f; // rotting corpse nearby
inline constexpr float kVillagerRestitution = 0.05f;

// --- original-asset overlay (M10: --bw <install-dir>) ---
// The landscape mapping fixes XZ at 1 B&W cell (10 B&W units) = 1 meter, so
// meshes at 0.1 m per B&W unit stand in true proportion to the ground.
inline constexpr float kBwMeshScale = 0.1f;
// Land altitude also arrives in B&W units; scaling above 0.1 exaggerates
// relief so the gentle originals read at our camera (calibrate by eye with
// --bw-report + a land boot; height is DATA, changing this only re-imports).
inline constexpr float kBwLandHeightScale = 0.22f;

}  // namespace tune
