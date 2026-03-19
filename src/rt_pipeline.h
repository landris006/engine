#pragma once

#include "context.h"
#include "scene.h"
#include "shared_types.h"

struct RtPipeline {
  vk::UniquePipeline rt_pipeline;
  vk::UniquePipeline tonemap_pipeline;
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

  AllocatedImage display_image;

  AllocatedImage skybox_image;
  vk::UniqueSampler skybox_sampler;
};

RtPipeline create_rt_pipeline(const Context& context, vk::Extent2D extent,
                              const Scene& scene,
                              const char* skybox_path = nullptr);

bool reload_pipeline(const Context& context, RtPipeline& p);

void update_camera(const Context& ctx, RtPipeline& p, const CameraUbo& cam);
