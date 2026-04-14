<h1 align="center">Placeholder until I make a proper README</h1>

## Features

  - Hardware ray tracing via VK_KHR_ray_tracing_pipeline / VK_KHR_acceleration_structure
  - Monte Carlo path tracing with progressive sample accumulation
  - Physically-based Cook-Torrance BRDF (GGX NDF, Fresnel-Schlick, VNDF importance sampling)
  - Next Event Estimation (NEE) with explicit emissive triangle sampling
  - Multiple Importance Sampling (MIS) between BRDF and NEE paths
  - HDR environment map lighting (equirectangular)
  - tonemapping + gamma correction + firefly clamping
  - glTF 2.0 / GLB loading (fastgltf) and Wavefront OBJ (tinyobjloader)
  - PBR material textures: albedo, metallic-roughness, normal, emissive
  - shader hot reloading
  - ImGui UI: camera editor, light controls, frame time graph (very basic so far)
  - Scene config persistence (scene.ini) with Ctrl+S save
  - Headless mode (engine_headless) for offline batch rendering to PNG


## Here are some static renders:

- 4 bounces, 1000 samples
![dir_light](renders/dir_light.png)
- 4 bounces, 4000 samples
![dir_light_4k](renders/dir_light_4k.png)
- 4 bounces, 1000 samples, no directional light
![output](renders/output.png)
