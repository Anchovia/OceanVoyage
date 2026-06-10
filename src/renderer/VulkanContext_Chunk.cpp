// The farm chunk voxel mesh builder (buildChunkBuffer) and its per-frame rebuild
// (rebuildDirtyChunks) lived here. Both were removed during the renderer/World
// decoupling: the all-water OceanVoyage world emits no chunk geometry (the mesher
// skipped WATER/AIR tiles), and the renderer no longer reads World tiles.
//
// This translation unit is now intentionally empty and will be dropped from the build
// (CMakeLists.txt) in the dead-asset cleanup pass at the end of the World decoupling.
