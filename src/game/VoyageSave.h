#pragma once
#include "game/GameState.h"
#include <string>

// OceanVoyage save format ("OVYG"). Replaces the legacy farm "PFRM" save:
// persists the sailing state (ship + game time) instead of player/inventory/
// chunks. Old PFRM files fail the magic check and are rejected as a new game.
namespace VoyageSave {

struct Data {
    float     gameTime = 0.0f;
    ShipState ship;
};

// Atomic write (.tmp then rename): a crash mid-write never damages the live
// save. Returns false if serialization failed (live file left untouched).
bool save(const std::string& path, const Data& data);

// Returns true and fills `out` only if the whole file reads back valid
// (magic, version, finite floats). On any failure `out` is untouched.
bool load(const std::string& path, Data& out);

} // namespace VoyageSave
