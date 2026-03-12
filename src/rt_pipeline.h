#pragma once

#include <slang-com-ptr.h>
#include <slang.h>

#include "camera.h"
#include "context.h"
#include "scene.h"

struct PushConstants {
  uint32_t sample_count;
  uint32_t max_bounces;
  float    time;
  uint32_t light_count;
};

struct RtPipeline {
  vk::UniquePipeline pipeline;
  vk::UniquePipelineLayout layout;
  vk::UniqueDescriptorSetLayout desc_layout;
  vk::UniqueDescriptorPool desc_pool;
  vk::DescriptorSet desc_set;

  vk::StridedDeviceAddressRegionKHR raygen_region;
  vk::StridedDeviceAddressRegionKHR miss_region;
  vk::StridedDeviceAddressRegionKHR hit_region;
  vk::StridedDeviceAddressRegionKHR callable_region;

  AllocatedImage storage_image;
  AllocatedBuffer sbt_buffer;
  AllocatedBuffer camera_ubo;
};

static auto compile_slang(const Context& ctx, const char* file,
                          const char* entry_name) -> vk::UniqueShaderModule {
  Slang::ComPtr<slang::IGlobalSession> global_session;
  slang::createGlobalSession(global_session.writeRef());

  slang::TargetDesc target_desc{
      .format = SLANG_SPIRV,
      .profile = global_session->findProfile("spirv_1_5"),
      .forceGLSLScalarBufferLayout = true,
  };

  const char* search_paths[] = {"shaders"};

  slang::SessionDesc session_desc{.targets = &target_desc,
                                  .targetCount = 1,
                                  .searchPaths = search_paths,
                                  .searchPathCount = 1};

  Slang::ComPtr<slang::ISession> session;
  global_session->createSession(session_desc, session.writeRef());

  Slang::ComPtr<slang::IBlob> diagnostics;
  auto* module = session->loadModule(file, diagnostics.writeRef());

  if (diagnostics) {
    fprintf(stderr, "[slang]: %s\n", (char*)diagnostics->getBufferPointer());
  }

  if (!module) {
    fprintf(stderr, "[slang]: Failed to load module\n");
    std::abort();
  }

  Slang::ComPtr<slang::IEntryPoint> entry_point;
  module->findEntryPointByName(entry_name, entry_point.writeRef());
  if (!entry_point) {
    fprintf(stderr, "[slang]: Failed to find entry point\n");
    std::abort();
  };

  slang::IComponentType* components[] = {module, entry_point.get()};
  Slang::ComPtr<slang::IComponentType> composite;
  session->createCompositeComponentType(components, 2, composite.writeRef());

  Slang::ComPtr<slang::IComponentType> linked;
  composite->link(linked.writeRef(), diagnostics.writeRef());
  if (diagnostics) {
    fprintf(stderr, "[slang]: %s\n", (char*)diagnostics->getBufferPointer());
  }

  Slang::ComPtr<slang::IBlob> code;
  linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef());
  if (diagnostics) {
    fprintf(stderr, "[slang]: %s\n", (char*)diagnostics->getBufferPointer());
  }

  return ctx.device->createShaderModuleUnique(
      vk::ShaderModuleCreateInfo{}
          .setCodeSize(code->getBufferSize())
          .setPCode((uint32_t*)code->getBufferPointer()));
}

static auto create_rt_pipeline(const Context& context,
                               const vk::Extent2D extent, const Scene& scene)
    -> RtPipeline {
  auto image = createImage(
      context, vk::Extent3D(extent.width, extent.height, 1),
      vk::Format::eR32G32B32A32Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc |
          vk::ImageUsageFlagBits::eTransferDst);

  submit_one_time_command(context, [&](const vk::CommandBuffer& buf) {
    auto barrier = vk::ImageMemoryBarrier()
                       .setOldLayout(vk::ImageLayout::eUndefined)
                       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                       .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                       .setImage(image.handle.get())
                       .setSubresourceRange(
                           vk::ImageSubresourceRange()
                               .setAspectMask(vk::ImageAspectFlagBits::eColor)
                               .setBaseMipLevel(0)
                               .setLevelCount(1)
                               .setBaseArrayLayer(0)
                               .setLayerCount(1))
                       .setSrcAccessMask({})
                       .setDstAccessMask({});
    buf.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                        barrier);

    auto subresource_range = vk::ImageSubresourceRange()
                                 .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                 .setBaseMipLevel(0)
                                 .setLevelCount(1)
                                 .setBaseArrayLayer(0)
                                 .setLayerCount(1);
    auto color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f};
    buf.clearColorImage(image.handle.get(),
                        vk::ImageLayout::eTransferDstOptimal, &color, 1,
                        &subresource_range);

    barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
    buf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eRayTracingShaderKHR, {}, {},
                        {}, barrier);
  });

  auto stage_flags = vk::ShaderStageFlagBits::eRaygenKHR |
                     vk::ShaderStageFlagBits::eMissKHR |
                     vk::ShaderStageFlagBits::eClosestHitKHR;

  std::array<vk::DescriptorSetLayoutBinding, 6> bindings = {{
      {0, vk::DescriptorType::eAccelerationStructureKHR, 1, stage_flags},
      {1, vk::DescriptorType::eStorageImage, 1, stage_flags},
      {2, vk::DescriptorType::eUniformBuffer, 1, stage_flags},
      {3, vk::DescriptorType::eStorageBuffer, 1, stage_flags},
      {4, vk::DescriptorType::eStorageBuffer, 1, stage_flags},
      {5, vk::DescriptorType::eStorageBuffer, 1, stage_flags},
  }};

  auto desc_layout = context.device->createDescriptorSetLayoutUnique(
      vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings));

  std::array<vk::DescriptorPoolSize, 4> pool_sizes = {{
      {vk::DescriptorType::eAccelerationStructureKHR, 1},
      {vk::DescriptorType::eStorageImage, 1},
      {vk::DescriptorType::eUniformBuffer, 1},
      {vk::DescriptorType::eStorageBuffer, 3},
  }};

  auto desc_pool = context.device->createDescriptorPoolUnique(
      vk::DescriptorPoolCreateInfo{}.setMaxSets(1).setPoolSizes(pool_sizes));

  auto desc_set = context.device->allocateDescriptorSets(
      vk::DescriptorSetAllocateInfo{}
          .setDescriptorPool(desc_pool.get())
          .setSetLayouts(desc_layout.get()))[0];

  vk::PushConstantRange pc_range{vk::ShaderStageFlagBits::eRaygenKHR, 0,
                                 sizeof(PushConstants)};

  auto layout = context.device->createPipelineLayoutUnique(
      vk::PipelineLayoutCreateInfo{}
          .setSetLayouts(desc_layout.get())
          .setPushConstantRanges(pc_range));

  auto raygen_module = compile_slang(context, "raygen", "main");
  auto miss_module = compile_slang(context, "miss", "main");
  auto shadow_module = compile_slang(context, "shadow_miss", "main");
  auto closesthit_module = compile_slang(context, "closesthit", "main");

  // clang-format off
  std::array<vk::PipelineShaderStageCreateInfo, 4> stages = {{
      {{}, vk::ShaderStageFlagBits::eRaygenKHR,     raygen_module.get(),     "main"},
      {{}, vk::ShaderStageFlagBits::eMissKHR,       miss_module.get(),       "main"},
      {{}, vk::ShaderStageFlagBits::eMissKHR,       shadow_module.get(),     "main"},
      {{}, vk::ShaderStageFlagBits::eClosestHitKHR, closesthit_module.get(), "main"},
  }};
  // clang-format on

  std::array<vk::RayTracingShaderGroupCreateInfoKHR, 4> groups = {{
      // raygen — GENERAL, stage 0
      vk::RayTracingShaderGroupCreateInfoKHR{}
          .setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
          .setGeneralShader(0)
          .setClosestHitShader(VK_SHADER_UNUSED_KHR)
          .setAnyHitShader(VK_SHADER_UNUSED_KHR)
          .setIntersectionShader(VK_SHADER_UNUSED_KHR),
      // miss — GENERAL, stage 1
      vk::RayTracingShaderGroupCreateInfoKHR{}
          .setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
          .setGeneralShader(1)
          .setClosestHitShader(VK_SHADER_UNUSED_KHR)
          .setAnyHitShader(VK_SHADER_UNUSED_KHR)
          .setIntersectionShader(VK_SHADER_UNUSED_KHR),
      // shadow miss — GENERAL, stage 2
      vk::RayTracingShaderGroupCreateInfoKHR{}
          .setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
          .setGeneralShader(2)
          .setClosestHitShader(VK_SHADER_UNUSED_KHR)
          .setAnyHitShader(VK_SHADER_UNUSED_KHR)
          .setIntersectionShader(VK_SHADER_UNUSED_KHR),
      // closesthit — TRIANGLES, stage 3
      vk::RayTracingShaderGroupCreateInfoKHR{}
          .setType(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup)
          .setGeneralShader(VK_SHADER_UNUSED_KHR)
          .setClosestHitShader(3)
          .setAnyHitShader(VK_SHADER_UNUSED_KHR)
          .setIntersectionShader(VK_SHADER_UNUSED_KHR),
  }};

  auto pipeline_result = context.device->createRayTracingPipelineKHRUnique(
      {}, {},
      vk::RayTracingPipelineCreateInfoKHR{}
          .setStages(stages)
          .setGroups(groups)
          .setMaxPipelineRayRecursionDepth(2)
          .setLayout(layout.get()));

  if (pipeline_result.result != vk::Result::eSuccess) {
    fprintf(stderr, "[vulkan] Error: Failed to create ray tracing pipeline\n");
    std::abort();
  }

  auto pipeline = std::move(pipeline_result.value);

  auto rt_props =
      context.physical_device
          .getProperties2<vk::PhysicalDeviceProperties2,
                          vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>()
          .get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

  uint32_t handle_size = rt_props.shaderGroupHandleSize;
  uint32_t handle_align = rt_props.shaderGroupHandleAlignment;
  uint32_t base_align = rt_props.shaderGroupBaseAlignment;

  auto align_up = [](uint32_t val, uint32_t align) {
    return (val + align - 1) & ~(align - 1);
  };

  uint32_t handle_stride = align_up(handle_size, handle_align);
  uint32_t raygen_size = align_up(handle_stride, base_align);    // 1 handle
  uint32_t miss_size = align_up(handle_stride * 2, base_align);  // 2 handles
  uint32_t hit_size = align_up(handle_stride, base_align);       // 1 handle
  uint32_t total_size = raygen_size + miss_size + hit_size;

  auto sbt_buffer =
      createBuffer(context, total_size,
                   vk::BufferUsageFlagBits::eShaderBindingTableKHR |
                       vk::BufferUsageFlagBits::eShaderDeviceAddress,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);

  // 4 groups × handle_size bytes each
  auto handles = context.device->getRayTracingShaderGroupHandlesKHR<uint8_t>(
      pipeline.get(), 0, 4, 4 * handle_size);

  {
    auto* sbt = (uint8_t*)context.device->mapMemory(sbt_buffer.memory.get(), 0,
                                                    total_size);
    memcpy(sbt, handles.data() + 0 * handle_size, handle_size);  // raygen
    memcpy(sbt + raygen_size, handles.data() + 1 * handle_size,
           handle_size);  // miss
    memcpy(sbt + raygen_size + handle_stride, handles.data() + 2 * handle_size,
           handle_size);  // shadow miss
    memcpy(sbt + raygen_size + miss_size, handles.data() + 3 * handle_size,
           handle_size);  // hit
    context.device->unmapMemory(sbt_buffer.memory.get());
  }

  vk::StridedDeviceAddressRegionKHR raygen_region{sbt_buffer.address,
                                                  raygen_size, raygen_size};
  vk::StridedDeviceAddressRegionKHR miss_region{
      sbt_buffer.address + raygen_size, handle_stride, miss_size};
  vk::StridedDeviceAddressRegionKHR hit_region{
      sbt_buffer.address + raygen_size + miss_size, hit_size, hit_size};

  auto camera_ubo = createBuffer(context, sizeof(CameraUbo),
                                 vk::BufferUsageFlagBits::eUniformBuffer,
                                 vk::MemoryPropertyFlagBits::eHostVisible |
                                     vk::MemoryPropertyFlagBits::eHostCoherent);

  vk::WriteDescriptorSetAccelerationStructureKHR tlas_info{};
  tlas_info.setAccelerationStructures(scene.tlas_handle.get());

  auto image_info = vk::DescriptorImageInfo()
                        .setImageLayout(vk::ImageLayout::eGeneral)
                        .setImageView(image.view.get());

  vk::DescriptorBufferInfo camera_info{camera_ubo.handle.get(), 0,
                                       sizeof(CameraUbo)};
  vk::DescriptorBufferInfo mesh_info{scene.mesh_info_buffer.handle.get(), 0,
                                     vk::WholeSize};
  vk::DescriptorBufferInfo mat_info{scene.material_buffer.handle.get(), 0,
                                    vk::WholeSize};
  vk::DescriptorBufferInfo light_buf_info{
      scene.light_triangle_buffer.handle.get(), 0, vk::WholeSize};

  std::array<vk::WriteDescriptorSet, 6> writes = {{
      // TLAS
      vk::WriteDescriptorSet{}
          .setDstSet(desc_set)
          .setDstBinding(0)
          .setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
          .setDescriptorCount(1)
          .setPNext(&tlas_info),
      // Storage image
      vk::WriteDescriptorSet{}
          .setDstSet(desc_set)
          .setDstBinding(1)
          .setDescriptorType(vk::DescriptorType::eStorageImage)
          .setDescriptorCount(1)
          .setPImageInfo(&image_info),
      // Camera
      vk::WriteDescriptorSet{}
          .setDstSet(desc_set)
          .setDstBinding(2)
          .setDescriptorType(vk::DescriptorType::eUniformBuffer)
          .setDescriptorCount(1)
          .setPBufferInfo(&camera_info),
      // Mesh info
      vk::WriteDescriptorSet{}
          .setDstSet(desc_set)
          .setDstBinding(3)
          .setDescriptorType(vk::DescriptorType::eStorageBuffer)
          .setDescriptorCount(1)
          .setPBufferInfo(&mesh_info),
      // Materials
      vk::WriteDescriptorSet{}
          .setDstSet(desc_set)
          .setDstBinding(4)
          .setDescriptorType(vk::DescriptorType::eStorageBuffer)
          .setDescriptorCount(1)
          .setPBufferInfo(&mat_info),
      // Light triangles
      vk::WriteDescriptorSet{}
          .setDstSet(desc_set)
          .setDstBinding(5)
          .setDescriptorType(vk::DescriptorType::eStorageBuffer)
          .setDescriptorCount(1)
          .setPBufferInfo(&light_buf_info),
  }};

  context.device->updateDescriptorSets(writes, {});

  return RtPipeline{
      .pipeline = std::move(pipeline),
      .layout = std::move(layout),
      .desc_layout = std::move(desc_layout),
      .desc_pool = std::move(desc_pool),
      .desc_set = desc_set,
      .raygen_region = raygen_region,
      .miss_region = miss_region,
      .hit_region = hit_region,
      .callable_region = {},
      .storage_image = std::move(image),
      .sbt_buffer = std::move(sbt_buffer),
      .camera_ubo = std::move(camera_ubo),
  };
}

static void update_camera(const Context& ctx, RtPipeline& p,
                          const CameraUbo& cam) {
  void* mapped =
      ctx.device->mapMemory(p.camera_ubo.memory.get(), 0, sizeof(CameraUbo));
  memcpy(mapped, &cam, sizeof(CameraUbo));
  ctx.device->unmapMemory(p.camera_ubo.memory.get());
}
