#pragma once

#include "Mesh.h"

// Placeholder mesh builders for the village slice. GL-free: everything
// returns MeshData built from the shared primitives (CCW winding, 9-float
// layout), exactly like the tree/rock/hand builders in main.cpp.
namespace models {

// Villager body parts, each modeled around its pivot so poses are single
// rotations: legs/arms pivot at hip/shoulder (mesh extends downward), torso
// and head are centered. ~1.95 units tall assembled.
MeshData villagerHead(int variant);
MeshData villagerTorso();  // near-white; job color arrives via uTint
MeshData villagerArm();
MeshData villagerLeg();

// Buildings.
MeshData temple();        // the god's seat: stepped platform + columns + roof
MeshData templeCrystal(); // the mana beacon (drawn emissive, glow = mana)
MeshData houseStage(int stage);  // 0 posts, 1 half walls, 2 walls, 3 roofed
MeshData houseWindows();         // emissive quads, drawn at night
MeshData totem();                // village center - the future worship site
MeshData storagePad();
MeshData woodPile();             // scaled by stock
MeshData foodPile();
MeshData campfire();             // stones + wood
MeshData campfireFlame();        // emissive cone, night flicker
MeshData fieldSlab(float halfX, float halfZ);
MeshData cropCone();             // per-cell, scaled by growth

// The building roster built from scaffolds.
MeshData largeAbode();
MeshData workshop();
MeshData store();
MeshData creche();
MeshData graveyard();
MeshData dispenser();
MeshData wonder();

// Haulable resource props.
MeshData logProp();
MeshData foodBundleProp();
MeshData stumpProp();
MeshData scaffoldProp();  // one lattice unit; stacks draw it repeatedly

// Thought bubbles (emissive billboards above heads).
MeshData bubbleHunger();
MeshData bubbleSleep();
MeshData bubbleFear();

}  // namespace models
