struct Vertex {
  float3 pos;
  float3 normal;
};

struct Material {
  float3 albedo;
  float3 emissive;
};

struct MeshInfo {
  uint64_t vertex_address;
  uint64_t index_address;
  uint32_t material_index;
  uint32_t pad;
};

struct CameraUbo {
  float4 origin;
  float4 lower_left;
  float4 horizontal;
  float4 vertical;
};

struct LightTriangle {
  float3 v0;
  float3 v1;
  float3 v2;
  float3 normal;
  float  area;
  uint   material_index;
};

struct PushConstants {
  uint  sample_count;
  uint  max_bounces;
  float time;
  uint  light_count;
};

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
};

struct ShadowPayload {
  bool occluded;
};
