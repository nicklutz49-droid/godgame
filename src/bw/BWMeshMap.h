#pragma once

// Which B&W mesh dresses which placeholder slot. Mesh indices are positions
// in AllMeshes.g3d's MESHES block - the vanilla, format-fixed mesh order
// (cross-checked against the openblack project's mesh-id listing). Names are
// kept for the --bw-report output only. The Celtic tribe wears our village;
// scaleMul rides on top of tune::kBwMeshScale for slots that need it once
// real-install bounding boxes come back via --bw-report.
namespace bw {

enum class Slot : int {
  Tree0,
  Tree1,
  Tree2,
  Rock0,
  Rock1,
  Rock2,
  House,  // the finished small abode (construction stages stay procedural)
  LargeAbode,
  Workshop,
  Store,
  Creche,
  Graveyard,
  Dispenser,
  Wonder,
  Totem,
  Temple,
  Scaffold,
  Campfire,
  Log,
  FoodBundle,
  Stump,
  WoodPile,
  FoodPile,
  Count
};

struct SlotBinding {
  Slot slot;
  int meshIndex;
  const char* bwName;
  float scaleMul;
};

inline constexpr SlotBinding kSlotBindings[] = {
    {Slot::Tree0, 583, "TreeOak", 1.0f},
    {Slot::Tree1, 576, "TreeCedar", 1.0f},
    {Slot::Tree2, 577, "TreeConifer", 1.0f},
    {Slot::Rock0, 380, "ObjectRockLimeStone", 1.0f},
    {Slot::Rock1, 392, "ObjectSquareRockLimeStone", 1.0f},
    {Slot::Rock2, 386, "ObjectSharpRockLimeStone", 1.0f},
    {Slot::House, 74, "BuildingCeltic1", 1.0f},
    {Slot::LargeAbode, 77, "BuildingCeltic4", 1.0f},
    {Slot::Workshop, 92, "BuildingCelticWorkshop", 1.0f},
    {Slot::Store, 223, "BuildingCelticStoragePit", 1.0f},
    {Slot::Creche, 80, "BuildingCelticCreche", 1.0f},
    {Slot::Graveyard, 83, "BuildingCelticGraveyard", 1.0f},
    {Slot::Dispenser, 558, "SpellSpellDispenser", 1.0f},
    {Slot::Wonder, 91, "BuildingCelticWonder", 1.0f},
    {Slot::Totem, 87, "BuildingCelticTotem", 1.0f},
    // The Celtic citadel has no single whole-temple mesh; the altar reads
    // best alone. Candidates if it disappoints: BuildingNorseTemple 215,
    // BuildingAztecTemple 70.
    {Slot::Temple, 95, "BuildingCitadelCelticAltar", 1.0f},
    {Slot::Scaffold, 157, "BuildingScaffold01", 1.0f},
    {Slot::Campfire, 148, "BuildingCampfire", 1.0f},
    {Slot::Log, 372, "ObjectLogsInHand", 1.0f},
    {Slot::FoodBundle, 533, "SpellGrainPile", 1.0f},
    {Slot::Stump, 592, "TreeRoots", 1.0f},
    {Slot::WoodPile, 232, "BuildingWood02", 1.0f},
    {Slot::FoodPile, 225, "BuildingGrain", 1.0f},
};

}  // namespace bw
