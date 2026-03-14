#include "../src/shared_types.h"

// Shader-only types (never accessed from C++)

// this is how closesthit communicates with raygen
struct HitPayload {
  float3 albedo;
  float3 emissive;
  float3 normal;
  float3 next_origin;
  float3 next_dir;
  bool   missed;
  uint   seed;
  float  hit_dist;
  float  light_area;
  float  metallic;
  float  roughness;
};

struct ShadowPayload {
  bool occluded;
};
