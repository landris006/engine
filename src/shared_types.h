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
};

struct Material {
  float3 albedo;
  float3 emissive;
  float metallic;
  float roughness;
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
  float time;
  uint32_t light_count;
};

#ifdef __cplusplus
static_assert(sizeof(Vertex) == 24);
static_assert(sizeof(Material) == 32);
static_assert(offsetof(Material, metallic) == 24);
static_assert(sizeof(MeshInfo) == 24);
static_assert(sizeof(CameraUbo) == 64);
static_assert(sizeof(LightTriangle) == 56);
static_assert(sizeof(PushConstants) == 16);
#endif
