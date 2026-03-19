#pragma once

#ifdef __cplusplus
#include <cstdint>
#include <glm/glm.hpp>
using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;
#endif

struct Vertex {
  float3 pos;
  float3 normal;
  float2 uv;
  float4 tangent;
};

struct Material {
  float4 base_color_factor;
  float3 emissive_factor;
  float metallic_factor;
  float roughness_factor;
  int32_t base_color_tex;
  int32_t metal_rough_tex;
  int32_t normal_tex;
  int32_t emissive_tex;
  uint32_t pad[3];
};

struct MeshInfo {
  uint64_t vertex_address;
  uint64_t index_address;
  uint32_t material_index;
  uint32_t pad = {};
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
  float area;
  uint32_t material_index;
};

struct PushConstants {
  uint32_t sample_count;
  uint32_t max_bounces;
  uint32_t light_count;
  float3 dir_light;
  float3 dir_light_radiance;
  float time;
};

#ifdef __cplusplus
static_assert(sizeof(Vertex) == 48);
static_assert(offsetof(Vertex, uv) == 24);
static_assert(offsetof(Vertex, tangent) == 32);

static_assert(sizeof(Material) == 64);

static_assert(sizeof(MeshInfo) == 24);
static_assert(sizeof(CameraUbo) == 64);
static_assert(sizeof(LightTriangle) == 56);
static_assert(sizeof(PushConstants) == 40);
#endif
