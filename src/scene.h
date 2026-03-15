#pragma once

#include <tinyobjloader/tiny_obj_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stbimage/stb_image.h>

#include <cstdint>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>  // teaches fastgltf about glm types
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <glm/fwd.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include <unordered_set>

#include "context.h"
#include "utils.h"

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include "shared_types.h"

static constexpr char ASSETS_DIR[] = "assets";

struct Mesh {
  AllocatedBuffer vertex_buffer;
  AllocatedBuffer index_buffer;
  vk::UniqueAccelerationStructureKHR blas_handle;
  AllocatedBuffer blas_buffer;
};

struct CpuMesh {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};

struct Scene {
  std::vector<Mesh> meshes;
  AllocatedBuffer mesh_info_buffer;
  AllocatedBuffer material_buffer;
  AllocatedBuffer light_triangle_buffer;
  uint32_t light_count = 0;

  AllocatedBuffer tlas_buffer;
  AllocatedBuffer tlas_instance_buffer;
  vk::UniqueAccelerationStructureKHR tlas_handle;

  std::vector<AllocatedImage> textures;
};

struct VertexHash {
  size_t operator()(const Vertex& v) const {
    size_t h = 0;
    h ^= std::hash<glm::vec3>()(v.pos) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<glm::vec3>()(v.normal) + 0x9e3779b9 + (h << 6) + (h >> 2);

    return h;
  };
};
struct VertexEqual {
  bool operator()(const Vertex& a, const Vertex& b) const {
    return a.pos == b.pos && a.normal == b.normal;
  };
};

static auto create_blas(const Context& ctx, const Mesh& mesh,
                        uint32_t vertex_count, uint32_t index_count)
    -> std::tuple<vk::UniqueAccelerationStructureKHR, AllocatedBuffer> {
  vk::AccelerationStructureGeometryKHR geometry{};
  geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
  geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
  geometry.geometry.triangles.setVertexFormat(vk::Format::eR32G32B32Sfloat)
      .setVertexData(mesh.vertex_buffer.address)
      .setVertexStride(sizeof(Vertex))
      .setMaxVertex(vertex_count - 1)
      .setIndexType(vk::IndexType::eUint32)
      .setIndexData(mesh.index_buffer.address);

  auto build_info =
      vk::AccelerationStructureBuildGeometryInfoKHR()
          .setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
          .setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
          .setGeometries(geometry);

  uint32_t primitve_count = index_count / 3;

  auto sizes = ctx.device->getAccelerationStructureBuildSizesKHR(
      vk::AccelerationStructureBuildTypeKHR::eDevice, build_info,
      primitve_count);

  auto buffer =
      createBuffer(ctx, sizes.accelerationStructureSize,
                   vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto handle = ctx.device->createAccelerationStructureKHRUnique(
      vk::AccelerationStructureCreateInfoKHR()
          .setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
          .setSize(sizes.accelerationStructureSize)
          .setBuffer(buffer.handle.get()));

  auto scratch = createBuffer(ctx, sizes.buildScratchSize,
                              vk::BufferUsageFlagBits::eStorageBuffer |
                                  vk::BufferUsageFlagBits::eShaderDeviceAddress,
                              vk::MemoryPropertyFlagBits::eDeviceLocal);

  build_info.setDstAccelerationStructure(handle.get());
  build_info.scratchData.setDeviceAddress(scratch.address);

  auto range_info =
      vk::AccelerationStructureBuildRangeInfoKHR().setPrimitiveCount(
          primitve_count);
  submit_one_time_command(ctx, [&](vk::CommandBuffer cmd) {
    cmd.buildAccelerationStructuresKHR(build_info, &range_info);
  });

  return {std::move(handle), std::move(buffer)};
}

static auto load_obj_scene(const Context& ctx, const char* obj_path = nullptr)
    -> Scene {
  tinyobj::ObjReader reader;
  tinyobj::ObjReaderConfig config;

  // set mtl search path to the directory containing the obj file
  std::string path_str(obj_path);
  auto slash = path_str.find_last_of("/\\");
  config.mtl_search_path =
      slash != std::string::npos ? path_str.substr(0, slash + 1) : "./";

  if (!reader.ParseFromFile(obj_path, config)) {
    if (!reader.Error().empty())
      fprintf(stderr, "tinyobj error: %s\n", reader.Error().c_str());
    std::abort();
  }
  if (!reader.Warning().empty())
    printf("tinyobj warn: %s\n", reader.Warning().c_str());

  auto& attrib = reader.GetAttrib();
  auto& shapes = reader.GetShapes();
  auto& materials = reader.GetMaterials();

  auto meshes = std::vector<Mesh>(shapes.size());
  auto mesh_info = std::vector<MeshInfo>(shapes.size());

  for (const auto& [i, shape] : shapes | std::views::enumerate) {
    std::unordered_map<Vertex, uint32_t, VertexHash, VertexEqual> vertex_map;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (const auto& index : shape.mesh.indices) {
      Vertex vertex{};
      vertex.pos = {
          attrib.vertices[3 * index.vertex_index + 0],
          attrib.vertices[3 * index.vertex_index + 1],
          attrib.vertices[3 * index.vertex_index + 2],
      };
      vertex.normal = {
          attrib.normals[3 * index.normal_index + 0],
          attrib.normals[3 * index.normal_index + 1],
          attrib.normals[3 * index.normal_index + 2],
      };
      vertex.uv = {0.0f, 0.0f};
      vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};

      auto [it, inserted] =
          vertex_map.insert({vertex, (uint32_t)vertices.size()});
      if (inserted) {
        vertices.push_back(vertex);
      }
      indices.push_back(it->second);
    };

    auto vertex_buffer =
        createBuffer(ctx, vertices.size() * sizeof(Vertex),
                     vk::BufferUsageFlagBits::eVertexBuffer |
                         vk::BufferUsageFlagBits::eStorageBuffer |
                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                         vk::BufferUsageFlagBits::
                             eAccelerationStructureBuildInputReadOnlyKHR,
                     vk::MemoryPropertyFlagBits::eHostVisible |
                         vk::MemoryPropertyFlagBits::eHostCoherent);
    uploadToBuffer(ctx, vertex_buffer, vertices.data(),
                   vertices.size() * sizeof(Vertex));

    auto index_buffer =
        createBuffer(ctx, indices.size() * sizeof(uint32_t),
                     vk::BufferUsageFlagBits::eIndexBuffer |
                         vk::BufferUsageFlagBits::eStorageBuffer |
                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                         vk::BufferUsageFlagBits::
                             eAccelerationStructureBuildInputReadOnlyKHR,
                     vk::MemoryPropertyFlagBits::eHostVisible |
                         vk::MemoryPropertyFlagBits::eHostCoherent);
    uploadToBuffer(ctx, index_buffer, indices.data(),
                   indices.size() * sizeof(uint32_t));

    //
    meshes[i] = Mesh{.vertex_buffer = std::move(vertex_buffer),
                     .index_buffer = std::move(index_buffer),
                     .blas_handle = {},
                     .blas_buffer = {}};
    mesh_info[i] = MeshInfo{
        .vertex_address = meshes[i].vertex_buffer.address,
        .index_address = meshes[i].index_buffer.address,
        .material_index =
            shape.mesh.material_ids.empty() || shape.mesh.material_ids[0] < 0
                ? 0u
                : (uint32_t)shape.mesh.material_ids[0],
    };

    auto [blas_handle, blas_buffer] =
        create_blas(ctx, meshes[i], vertices.size(), indices.size());
    meshes[i].blas_handle = std::move(blas_handle);
    meshes[i].blas_buffer = std::move(blas_buffer);
  }

  auto instances =
      std::vector<vk::AccelerationStructureInstanceKHR>(meshes.size());
  for (const auto& [i, mesh] : meshes | std::views::enumerate) {
    vk::TransformMatrixKHR identity{};
    identity.matrix[0][0] = 1.f;
    identity.matrix[1][1] = 1.f;
    identity.matrix[2][2] = 1.f;

    auto blas_addr =
        ctx.device->getAccelerationStructureAddressKHR(mesh.blas_handle.get());

    instances[i]
        .setTransform(identity)
        .setInstanceCustomIndex(i)
        .setMask(0xFF)
        .setInstanceShaderBindingTableRecordOffset(0)
        .setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
        .setAccelerationStructureReference(blas_addr);
  }

  auto tlas_instance_buffer = createBuffer(
      ctx, instances.size() * sizeof(vk::AccelerationStructureInstanceKHR),
      vk::BufferUsageFlagBits::eShaderDeviceAddress |
          vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent);
  uploadToBuffer(
      ctx, tlas_instance_buffer, instances.data(),
      instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));

  vk::AccelerationStructureGeometryInstancesDataKHR instances_data{};
  instances_data.data.setDeviceAddress(tlas_instance_buffer.address);

  auto tlas_geom = vk::AccelerationStructureGeometryKHR{}
                       .setGeometryType(vk::GeometryTypeKHR::eInstances)
                       .setGeometry(instances_data);

  uint32_t instance_count = instances.size();
  auto tlas_build_info =
      vk::AccelerationStructureBuildGeometryInfoKHR{}
          .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
          .setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
          .setGeometries(tlas_geom);

  auto tlas_sizes = ctx.device->getAccelerationStructureBuildSizesKHR(
      vk::AccelerationStructureBuildTypeKHR::eDevice, tlas_build_info,
      instance_count);

  auto tlas_buffer =
      createBuffer(ctx, tlas_sizes.accelerationStructureSize,
                   vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto tlas_handle = ctx.device->createAccelerationStructureKHRUnique(
      vk::AccelerationStructureCreateInfoKHR{}
          .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
          .setSize(tlas_sizes.accelerationStructureSize)
          .setBuffer(tlas_buffer.handle.get()));

  auto tlas_scratch =
      createBuffer(ctx, tlas_sizes.buildScratchSize,
                   vk::BufferUsageFlagBits::eStorageBuffer |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eDeviceLocal);

  tlas_build_info.setDstAccelerationStructure(tlas_handle.get());
  tlas_build_info.scratchData.setDeviceAddress(tlas_scratch.address);

  auto tlas_range =
      vk::AccelerationStructureBuildRangeInfoKHR{}.setPrimitiveCount(
          instance_count);
  submit_one_time_command(ctx, [&](vk::CommandBuffer cmd) {
    cmd.buildAccelerationStructuresKHR(tlas_build_info, &tlas_range);
  });

  auto mesh_info_buffer =
      createBuffer(ctx, mesh_info.size() * sizeof(MeshInfo),
                   vk::BufferUsageFlagBits::eStorageBuffer |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  uploadToBuffer(ctx, mesh_info_buffer, mesh_info.data(),
                 mesh_info.size() * sizeof(MeshInfo));

  // Always keep at least one placeholder so buffers are never zero-sized.
  // Index 0 is either the first real material or the fallback white diffuse.
  auto mats = std::vector<Material>();
  if (materials.empty()) {
    mats.push_back(Material{
        .base_color_factor = {0.8f, 0.8f, 0.8f, 1.0f},
        .emissive_factor = {},
        .metallic_factor = 0.0f,
        .roughness_factor = 1.0f,
    });
  } else {
    mats.resize(materials.size());
    for (const auto& [i, mat] : materials | std::views::enumerate) {
      Material m{};
      m.base_color_factor = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2],
                             1.0f};

      if (mat.name == "Light") {
        m.emissive_factor = {15.0f, 15.0f, 15.0f};
      } else {
        m.emissive_factor = {0.0f, 0.0f, 0.0f};
      }

      if (mat.roughness > 0.0f) {
        m.roughness_factor = mat.roughness;
      } else {
        m.roughness_factor = 1.0f;
      }

      m.metallic_factor = mat.metallic;

      m.normal_tex = -1;
      m.emissive_tex = -1;
      m.metal_rough_tex = -1;
      m.base_color_tex = -1;

      mats[i] = m;
    }
  }

  auto material_buffer =
      createBuffer(ctx, mats.size() * sizeof(Material),
                   vk::BufferUsageFlagBits::eStorageBuffer |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  uploadToBuffer(ctx, material_buffer, mats.data(),
                 mats.size() * sizeof(Material));

  std::vector<LightTriangle> light_tris;
  for (size_t si = 0; si < shapes.size(); si++) {
    const auto& mesh_indices = shapes[si].mesh;
    if (mesh_indices.material_ids.empty() || mesh_indices.material_ids[0] < 0) {
      continue;
    }
    uint32_t mat_idx = (uint32_t)mesh_indices.material_ids[0];

    if (mats[mat_idx].emissive_factor == glm::vec3(0)) {
      continue;
    }

    for (size_t fi = 0; fi < mesh_indices.num_face_vertices.size(); fi++) {
      int i0 = mesh_indices.indices[fi * 3 + 0].vertex_index;
      int i1 = mesh_indices.indices[fi * 3 + 1].vertex_index;
      int i2 = mesh_indices.indices[fi * 3 + 2].vertex_index;
      glm::vec3 p0 = {attrib.vertices[3 * i0], attrib.vertices[3 * i0 + 1],
                      attrib.vertices[3 * i0 + 2]};
      glm::vec3 p1 = {attrib.vertices[3 * i1], attrib.vertices[3 * i1 + 1],
                      attrib.vertices[3 * i1 + 2]};
      glm::vec3 p2 = {attrib.vertices[3 * i2], attrib.vertices[3 * i2 + 1],
                      attrib.vertices[3 * i2 + 2]};
      glm::vec3 e1 = p1 - p0, e2 = p2 - p0;
      glm::vec3 n = glm::cross(e1, e2);
      float area = glm::length(n) * 0.5f;
      light_tris.push_back({p0, p1, p2, glm::normalize(n), area, mat_idx});
    }
  }

  uint32_t light_count = (uint32_t)light_tris.size();
  auto light_triangle_buffer = createBuffer(
      ctx, std::max(light_tris.size(), size_t(1)) * sizeof(LightTriangle),
      vk::BufferUsageFlagBits::eStorageBuffer |
          vk::BufferUsageFlagBits::eShaderDeviceAddress,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent);
  if (light_count > 0) {
    uploadToBuffer(ctx, light_triangle_buffer, light_tris.data(),
                   light_count * sizeof(LightTriangle));
  }

  return Scene{
      .meshes = std::move(meshes),
      .mesh_info_buffer = std::move(mesh_info_buffer),
      .material_buffer = std::move(material_buffer),
      .light_triangle_buffer = std::move(light_triangle_buffer),
      .light_count = light_count,
      .tlas_buffer = std::move(tlas_buffer),
      .tlas_instance_buffer = std::move(tlas_instance_buffer),
      .tlas_handle = std::move(tlas_handle),
  };
};

static auto build_tlas(const Context& ctx, const fastgltf::Asset& asset,
                       const std::vector<Mesh>& meshes,
                       const std::vector<MeshInfo>& mesh_info,
                       const std::vector<CpuMesh>& cpu_meshes,
                       const std::vector<Material>& materials)
    -> std::tuple<vk::UniqueAccelerationStructureKHR, AllocatedBuffer,
                  AllocatedBuffer, std::vector<LightTriangle>> {
  std::vector<std::pair<size_t, glm::mat4>> instances;

  std::vector<size_t> mesh_prim_offsets;
  {
    size_t offset = 0;
    for (const auto& mesh : asset.meshes) {
      mesh_prim_offsets.push_back(offset);
      offset += mesh.primitives.size();
    }
  }

  std::function<void(size_t, glm::mat4)> traverse = [&](size_t node_idx,
                                                        glm::mat4 parent) {
    const auto& node = asset.nodes[node_idx];

    glm::mat4 local = std::visit(
        fastgltf::visitor{
            [](const fastgltf::math::fmat4x4& m) {
              return glm::make_mat4(m.data());
            },
            [](const fastgltf::TRS& trs) {
              auto t =
                  glm::translate(glm::mat4(1), glm::vec3(trs.translation[0],
                                                         trs.translation[1],
                                                         trs.translation[2]));
              auto r =
                  glm::mat4_cast(glm::quat(trs.rotation[3], trs.rotation[0],
                                           trs.rotation[1], trs.rotation[2]));
              auto s = glm::scale(
                  glm::mat4(1),
                  glm::vec3(trs.scale[0], trs.scale[1], trs.scale[2]));
              return t * r * s;
            },
        },
        node.transform);

    glm::mat4 world = parent * local;

    if (node.meshIndex.has_value()) {
      size_t gltf_mesh_idx = node.meshIndex.value();
      size_t prim_count = asset.meshes[gltf_mesh_idx].primitives.size();
      for (size_t p = 0; p < prim_count; p++) {
        instances.push_back({mesh_prim_offsets[gltf_mesh_idx] + p, world});
      }
    }

    for (auto child : node.children) traverse(child, world);
  };

  for (auto root : asset.scenes[0].nodeIndices) traverse(root, glm::mat4(1.0f));

  // Build TLAS instances
  std::vector<vk::AccelerationStructureInstanceKHR> vk_instances;
  vk_instances.reserve(instances.size());

  for (auto& [mesh_idx, world] : instances) {
    vk::TransformMatrixKHR t{};
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 4; c++)
        t.matrix[r][c] = world[c][r];  // GLM column-major → Vulkan row-major

    auto blas_addr = ctx.device->getAccelerationStructureAddressKHR(
        meshes[mesh_idx].blas_handle.get());

    vk_instances.emplace_back(
        vk::AccelerationStructureInstanceKHR()
            .setTransform(t)
            .setInstanceCustomIndex(mesh_idx)
            .setMask(0xFF)
            .setInstanceShaderBindingTableRecordOffset(0)
            .setFlags(
                vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
            .setAccelerationStructureReference(blas_addr));
  }

  std::vector<LightTriangle> light_tris;
  for (auto& [mesh_idx, world] : instances) {
    uint32_t mat_idx = mesh_info[mesh_idx].material_index;

    if (materials[mat_idx].emissive_factor == glm::vec3(0) ||
        materials[mat_idx].emissive_tex >= 0 /* skip emission maps for now */) {
      continue;
    }

    auto& cpu = cpu_meshes[mesh_idx];
    for (size_t i = 0; i + 2 < cpu.indices.size(); i += 3) {
      glm::vec3 p0 =
          glm::vec3(world * glm::vec4(cpu.vertices[cpu.indices[i + 0]].pos, 1));
      glm::vec3 p1 =
          glm::vec3(world * glm::vec4(cpu.vertices[cpu.indices[i + 1]].pos, 1));
      glm::vec3 p2 =
          glm::vec3(world * glm::vec4(cpu.vertices[cpu.indices[i + 2]].pos, 1));

      glm::vec3 e1 = p1 - p0;
      glm::vec3 e2 = p2 - p0;
      glm::vec3 n = glm::cross(e1, e2);

      float area = glm::length(n) * 0.5f;

      light_tris.push_back({p0, p1, p2, glm::normalize(n), area, mat_idx});
    }
  }

  auto tlas_instance_buffer = createBuffer(
      ctx, vk_instances.size() * sizeof(vk::AccelerationStructureInstanceKHR),
      vk::BufferUsageFlagBits::eShaderDeviceAddress |
          vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent);
  uploadToBuffer(
      ctx, tlas_instance_buffer, vk_instances.data(),
      vk_instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));

  vk::AccelerationStructureGeometryInstancesDataKHR instances_data{};
  instances_data.data.setDeviceAddress(tlas_instance_buffer.address);

  auto tlas_geom = vk::AccelerationStructureGeometryKHR{}
                       .setGeometryType(vk::GeometryTypeKHR::eInstances)
                       .setGeometry(instances_data);

  uint32_t instance_count = vk_instances.size();
  auto tlas_build_info =
      vk::AccelerationStructureBuildGeometryInfoKHR{}
          .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
          .setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
          .setGeometries(tlas_geom);

  auto tlas_sizes = ctx.device->getAccelerationStructureBuildSizesKHR(
      vk::AccelerationStructureBuildTypeKHR::eDevice, tlas_build_info,
      instance_count);

  auto tlas_buffer =
      createBuffer(ctx, tlas_sizes.accelerationStructureSize,
                   vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto tlas_handle = ctx.device->createAccelerationStructureKHRUnique(
      vk::AccelerationStructureCreateInfoKHR{}
          .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
          .setSize(tlas_sizes.accelerationStructureSize)
          .setBuffer(tlas_buffer.handle.get()));

  auto tlas_scratch =
      createBuffer(ctx, tlas_sizes.buildScratchSize,
                   vk::BufferUsageFlagBits::eStorageBuffer |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eDeviceLocal);

  tlas_build_info.setDstAccelerationStructure(tlas_handle.get());
  tlas_build_info.scratchData.setDeviceAddress(tlas_scratch.address);

  auto tlas_range =
      vk::AccelerationStructureBuildRangeInfoKHR{}.setPrimitiveCount(
          instance_count);
  submit_one_time_command(ctx, [&](vk::CommandBuffer cmd) {
    cmd.buildAccelerationStructuresKHR(tlas_build_info, &tlas_range);
  });

  return {
      std::move(tlas_handle),
      std::move(tlas_buffer),
      std::move(tlas_instance_buffer),
      std::move(light_tris),
  };
}

static auto load_meshes(const Context& ctx, const fastgltf::Asset& asset)
    -> std::tuple<std::vector<Mesh>, std::vector<MeshInfo>,
                  std::vector<CpuMesh>> {
  auto meshes = std::vector<Mesh>();
  auto mesh_info = std::vector<MeshInfo>();
  auto cpu_meshes = std::vector<CpuMesh>();

  for (const auto& mesh : asset.meshes) {
    for (const auto& prim : mesh.primitives) {
      std::vector<Vertex> vertices;
      std::vector<uint32_t> indices;

      // POSITION (required)
      auto& pos_accessor =
          asset.accessors[prim.findAttribute("POSITION")->accessorIndex];
      vertices.resize(pos_accessor.count);
      fastgltf::iterateAccessorWithIndex<glm::vec3>(
          asset, pos_accessor,
          [&](glm::vec3 v, size_t i) { vertices[i].pos = v; });

      // NORMAL
      if (auto it = prim.findAttribute("NORMAL"); it != prim.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            asset, asset.accessors[it->accessorIndex],
            [&](glm::vec3 n, size_t i) { vertices[i].normal = n; });
      }

      // TEXCOORD_0
      if (auto it = prim.findAttribute("TEXCOORD_0");
          it != prim.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            asset, asset.accessors[it->accessorIndex],
            [&](glm::vec2 uv, size_t i) { vertices[i].uv = uv; });
      }

      // TANGENT
      if (auto it = prim.findAttribute("TANGENT");
          it != prim.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset, asset.accessors[it->accessorIndex],
            [&](glm::vec4 t, size_t i) { vertices[i].tangent = t; });
      } else {
        for (auto& v : vertices) {
          v.tangent = {1, 0, 0, 1};
        }
      }

      // Indices
      assert(prim.indicesAccessor.has_value());
      fastgltf::iterateAccessorWithIndex<uint32_t>(
          asset, asset.accessors[prim.indicesAccessor.value()],
          [&](uint32_t idx, size_t) { indices.push_back(idx); });

      // Upload buffers + build BLAS
      auto vertex_buffer =
          createBuffer(ctx, vertices.size() * sizeof(Vertex),
                       vk::BufferUsageFlagBits::eVertexBuffer |
                           vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eShaderDeviceAddress |
                           vk::BufferUsageFlagBits::
                               eAccelerationStructureBuildInputReadOnlyKHR,
                       vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent);
      uploadToBuffer(ctx, vertex_buffer, vertices.data(),
                     vertices.size() * sizeof(Vertex));

      auto index_buffer =
          createBuffer(ctx, indices.size() * sizeof(uint32_t),
                       vk::BufferUsageFlagBits::eIndexBuffer |
                           vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eShaderDeviceAddress |
                           vk::BufferUsageFlagBits::
                               eAccelerationStructureBuildInputReadOnlyKHR,
                       vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent);
      uploadToBuffer(ctx, index_buffer, indices.data(),
                     indices.size() * sizeof(uint32_t));

      Mesh m{
          .vertex_buffer = std::move(vertex_buffer),
          .index_buffer = std::move(index_buffer),
      };

      auto [blas_handle, blas_buffer] =
          create_blas(ctx, m, vertices.size(), indices.size());
      m.blas_handle = std::move(blas_handle);
      m.blas_buffer = std::move(blas_buffer);

      mesh_info.push_back(MeshInfo{
          .vertex_address = m.vertex_buffer.address,
          .index_address = m.index_buffer.address,
          .material_index = (uint32_t)prim.materialIndex.value_or(0),
      });

      cpu_meshes.push_back(CpuMesh{vertices, indices});
      meshes.push_back(std::move(m));
    }
  }

  return {std::move(meshes), std::move(mesh_info), std::move(cpu_meshes)};
}

static auto load_materials(const fastgltf::Asset& asset)
    -> std::vector<Material> {
  auto materials = std::vector<Material>();

  materials.reserve(asset.materials.size());
  for (const auto& material : asset.materials) {
    Material m{};

    // Albedo
    auto bc = material.pbrData.baseColorFactor;
    m.base_color_factor = {bc[0], bc[1], bc[2], bc[3]};
    m.metallic_factor = material.pbrData.metallicFactor;
    m.roughness_factor = material.pbrData.roughnessFactor;

    // Emissive
    auto ef = material.emissiveFactor;
    m.emissive_factor = {ef[0], ef[1], ef[2]};

    // Textures
    auto tex_index =
        [&](const std::optional<fastgltf::TextureInfo>& info) -> int32_t {
      if (!info.has_value()) {
        return -1;
      }
      return (int32_t)asset.textures[info->textureIndex].imageIndex.value();
    };

    m.base_color_tex = tex_index(material.pbrData.baseColorTexture);
    m.metal_rough_tex = tex_index(material.pbrData.metallicRoughnessTexture);
    m.emissive_tex = tex_index(material.emissiveTexture);
    m.normal_tex =
        material.normalTexture.has_value()
            ? (int32_t)asset.textures[material.normalTexture->textureIndex]
                  .imageIndex.value()

            : -1;

    materials.push_back(std::move(m));
  }

  return materials;
}

static auto load_textures(const Context& ctx, const fastgltf::Asset& asset,
                          std::filesystem::path path,
                          const std::unordered_set<size_t>& srgb_images)
    -> std::vector<AllocatedImage> {
  auto textures = std::vector<AllocatedImage>();

  textures.reserve(asset.images.size());
  for (const auto& [img_idx, image] : asset.images | std::views::enumerate) {
    bool srgb = srgb_images.contains(img_idx);
    std::visit(
        fastgltf::visitor{
            [](auto&) {},
            [&](const fastgltf::sources::URI& file_path) {
              // We don't support offsets with stbi.
              assert(file_path.fileByteOffset == 0);
              // We're only capable of
              // loading local files.
              assert(file_path.uri.isLocalPath());

              int width, height, nrChannels;
              auto parent_dir = std::filesystem::path(path).parent_path();
              std::string full_path = std::string(parent_dir) + "/" +
                                      std::string(file_path.uri.path().begin(),
                                                  file_path.uri.path().end());

              unsigned char* data =
                  stbi_load(full_path.c_str(), &width, &height, &nrChannels, 4);

              if (!data) {
                fprintf(stderr, "stbi_load failed: %s\n",
                        stbi_failure_reason());
                std::exit(1);
              }

              auto texture = load_texture(ctx, data, width, height, srgb);
              textures.push_back(std::move(texture));

              stbi_image_free(data);
            },
            [&](const fastgltf::sources::Array& vector) {
              int width, height, nrChannels;
              unsigned char* data = stbi_load_from_memory(
                  reinterpret_cast<const stbi_uc*>(vector.bytes.data()),
                  static_cast<int>(vector.bytes.size()), &width, &height,
                  &nrChannels, 4);

              if (!data) {
                fprintf(stderr, "stbi_load failed: %s\n",
                        stbi_failure_reason());
                std::exit(1);
              }

              auto texture = load_texture(ctx, data, width, height, srgb);
              textures.push_back(std::move(texture));

              stbi_image_free(data);
            },
            [&](const fastgltf::sources::BufferView& view) {
              auto& bufferView = asset.bufferViews[view.bufferViewIndex];
              auto& buffer = asset.buffers[bufferView.bufferIndex];

              std::visit(
                  fastgltf::visitor{
                      [](auto&) {},
                      [&](const fastgltf::sources::Array& vector) {
                        int width, height, nrChannels;
                        unsigned char* data = stbi_load_from_memory(
                            reinterpret_cast<const stbi_uc*>(
                                vector.bytes.data() + bufferView.byteOffset),
                            static_cast<int>(bufferView.byteLength), &width,
                            &height, &nrChannels, 4);

                        if (!data) {
                          fprintf(stderr, "stbi_load failed: %s\n",
                                  stbi_failure_reason());
                          std::exit(1);
                        }

                        auto texture =
                            load_texture(ctx, data, width, height, srgb);
                        textures.push_back(std::move(texture));

                        stbi_image_free(data);
                      }},
                  buffer.data);
            },
        },
        image.data);
  }

  return textures;
}

static auto load_gltf_scene(const Context& ctx, std::filesystem::path path)
    -> Scene {
  if (!std::filesystem::exists(path)) {
    std::cout << "Failed to find " << path << '\n';
    std::exit(1);
  }

  if constexpr (std::is_same_v<std::filesystem::path::value_type, wchar_t>) {
    std::wcout << "Loading " << path << '\n';
  } else {
    std::cout << "Loading " << path << '\n';
  }

  static constexpr auto supportedExtensions =
      fastgltf::Extensions::KHR_mesh_quantization |
      fastgltf::Extensions::KHR_texture_transform |
      fastgltf::Extensions::KHR_materials_variants;

  fastgltf::Parser parser(supportedExtensions);

  constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember |
                               fastgltf::Options::AllowDouble |
                               fastgltf::Options::LoadExternalBuffers |
                               fastgltf::Options::LoadExternalImages |
                               fastgltf::Options::GenerateMeshIndices;

  auto gltfFile = fastgltf::MappedGltfFile::FromPath(path);
  if (!bool(gltfFile)) {
    std::cerr << "Failed to open glTF file: "
              << fastgltf::getErrorMessage(gltfFile.error()) << '\n';
    std::exit(1);
  }

  auto asset = parser.loadGltf(gltfFile.get(), path.parent_path(), gltfOptions);
  if (asset.error() != fastgltf::Error::None) {
    std::cerr << "Failed to load glTF: "
              << fastgltf::getErrorMessage(asset.error()) << '\n';
    std::exit(1);
  }

  // Collect image indices used as base color or emissive (sRGB in glTF spec).
  std::unordered_set<size_t> srgb_images;
  for (const auto& mat : asset->materials) {
    auto add = [&](const auto& tex_info) {
      if (tex_info.has_value())
        srgb_images.insert(
            asset->textures[tex_info->textureIndex].imageIndex.value());
    };
    // based on glTF spec, base color and emission textures are sRGB
    add(mat.pbrData.baseColorTexture);
    add(mat.emissiveTexture);
  }

  auto textures = load_textures(ctx, asset.get(), path, srgb_images);
  printf("Loaded %zu texture(s)\n", textures.size());

  auto materials = load_materials(asset.get());
  printf("Loaded %zu material(s)\n", materials.size());

  if (materials.empty()) {
    materials.push_back(Material{
        .base_color_factor = {0.1f, 0.8f, 0.8f, 1.0f},
        .roughness_factor = 1.0f,
    });
  }

  auto [meshes, mesh_info, cpu_meshes] = load_meshes(ctx, asset.get());
  printf("Loaded %zu mesh(es)\n", meshes.size());

  auto [tlas_handle, tlas_buffer, tlas_instance_buffer, light_tris] =
      build_tlas(ctx, asset.get(), meshes, mesh_info, cpu_meshes, materials);

  auto mesh_info_buffer =
      createBuffer(ctx, mesh_info.size() * sizeof(MeshInfo),
                   vk::BufferUsageFlagBits::eStorageBuffer |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  uploadToBuffer(ctx, mesh_info_buffer, mesh_info.data(),
                 mesh_info.size() * sizeof(MeshInfo));

  auto material_buffer =
      createBuffer(ctx, materials.size() * sizeof(Material),
                   vk::BufferUsageFlagBits::eStorageBuffer |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  uploadToBuffer(ctx, material_buffer, materials.data(),
                 materials.size() * sizeof(Material));

  uint32_t light_count = (uint32_t)light_tris.size();
  printf("light_count: %u\n", light_count);
  auto light_triangle_buffer = createBuffer(
      ctx, std::max(light_tris.size(), size_t(1)) * sizeof(LightTriangle),
      vk::BufferUsageFlagBits::eStorageBuffer |
          vk::BufferUsageFlagBits::eShaderDeviceAddress,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent);
  if (light_count > 0) {
    uploadToBuffer(ctx, light_triangle_buffer, light_tris.data(),
                   light_count * sizeof(LightTriangle));
  }

  return Scene{
      .meshes = std::move(meshes),
      .mesh_info_buffer = std::move(mesh_info_buffer),
      .material_buffer = std::move(material_buffer),
      .light_triangle_buffer = std::move(light_triangle_buffer),
      .light_count = light_count,
      .tlas_buffer = std::move(tlas_buffer),
      .tlas_instance_buffer = std::move(tlas_instance_buffer),
      .tlas_handle = std::move(tlas_handle),
      .textures = std::move(textures),
  };
};

static auto create_scene(const Context& context, const char* model_path)
    -> Scene {
  // check if .obj/.gltf
  auto pa = std::filesystem::path(model_path).extension();
  if (pa == ".obj") {
    return load_obj_scene(context, model_path);
  }
  if (pa == ".gltf" || pa == ".glb") {
    return load_gltf_scene(context, model_path);
  }

  printf("Unknown file extension: %s\n", pa.string().c_str());
  std::exit(1);
};
