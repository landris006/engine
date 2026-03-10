#pragma once

#include <tinyobjloader/tiny_obj_loader.h>

#include <cstdint>

#include "context.h"
#include "vulkan/vulkan.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

static constexpr char ASSETS_DIR[] = "assets";

struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
};

struct Material {
  glm::vec3 albedo;
  glm::vec3 emissive;
};

struct MeshInfo {
  vk::DeviceAddress vertex_address;
  vk::DeviceAddress index_address;
  uint32_t material_index;
  uint32_t _pad = {};
};

struct Mesh {
  AllocatedBuffer vertex_buffer;
  AllocatedBuffer index_buffer;
  vk::UniqueAccelerationStructureKHR blas_handle;
  AllocatedBuffer blas_buffer;
};

struct Scene {
  std::vector<Mesh> meshes;
  AllocatedBuffer mesh_info_buffer;
  AllocatedBuffer material_buffer;

  AllocatedBuffer tlas_buffer;
  AllocatedBuffer tlas_instance_buffer;
  vk::UniqueAccelerationStructureKHR tlas_handle;
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

static auto create_scene(const Context& ctx) -> Scene {
  tinyobj::ObjReader reader;
  tinyobj::ObjReaderConfig config;
  config.mtl_search_path = "assets/";

  if (!reader.ParseFromFile("assets/cornell.obj", config)) {
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
        .material_index = (uint32_t)shape.mesh.material_ids[0],
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

  auto mats = std::vector<Material>(materials.size());
  for (const auto& [i, mat] : materials | std::views::enumerate) {
    Material m{};
    m.albedo = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]};

    if (mat.name == "Light")
      m.emissive = {15.0f, 15.0f, 15.0f};
    else
      m.emissive = {0.0f, 0.0f, 0.0f};

    mats[i] = m;
  }

  auto material_buffer =
      createBuffer(ctx, mats.size() * sizeof(Material),
                   vk::BufferUsageFlagBits::eStorageBuffer |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  uploadToBuffer(ctx, material_buffer, mats.data(),
                 mats.size() * sizeof(Material));

  return Scene{
      .meshes = std::move(meshes),
      .mesh_info_buffer = std::move(mesh_info_buffer),
      .material_buffer = std::move(material_buffer),
      .tlas_buffer = std::move(tlas_buffer),
      .tlas_instance_buffer = std::move(tlas_instance_buffer),
      .tlas_handle = std::move(tlas_handle),
  };
};
