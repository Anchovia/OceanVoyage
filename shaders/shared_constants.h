#ifndef SHARED_CONSTANTS_H
#define SHARED_CONSTANTS_H

// Single source for constants shared between C++ and GLSL. This header is
// plain #defines only, so it compiles in both languages: C++ includes it from
// VulkanContext.h, shaders include it via glslc -I (GL_GOOGLE_include_directive).
// Change a value here and every shader + the renderer pick it up together.

// Tessendorf FFT ocean
#define SHARED_OCEAN_FFT_N           512                    // FFT resolution per axis (power of two)
#define SHARED_OCEAN_CASCADES        3                      // multi-scale cascade count
#define SHARED_OCEAN_CASCADE_L       2048.0, 512.0, 128.0   // world size (m) per cascade tile, largest -> smallest

// World water plane height (ocean rest level, buoyancy + reflection plane)
#define SHARED_SEA_LEVEL             0.5

// Ship wake simulation mask
#define SHARED_OCEAN_WAKE_N          1024                   // mask resolution per axis
#define SHARED_OCEAN_WAKE_WORLD_SIZE 1024.0                 // world size (m) the mask covers

// Cascaded shadow maps
#define SHARED_SHADOW_MAP_SIZE       2048                   // texels per cascade layer

// Small forward local-light set for ports and water highlights
#define SHARED_LOCAL_LIGHT_COUNT     8
#define SHARED_SPOT_LIGHT_COUNT      4

// Island waterline ellipses shared with the ocean shader (shore tint + foam)
#define SHARED_ISLAND_COUNT          4

#endif // SHARED_CONSTANTS_H
