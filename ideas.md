# Feature Roadmap: Path Tracer Learning

## Context
Goal is learning path tracing and graphics concepts through incremental implementation.
Current state: Lambertian Cornell box, NEE+MIS, hardware RT, progressive accumulation.

---

## Tier 1 — Shader-only, ~1-2h each

### Tone mapping - DONE
**Learns:** HDR → display color pipeline, gamma correction
- Raw linear HDR looks washed out and clipped on SDR monitors
- Apply ACES or Reinhard curve + gamma 2.2 in raygen before writing output_image
- Add exposure control via push constant
- Files: `raygen.slang`

### Sky / environment color
**Learns:** miss shader role, sky models
- Replace black sky with a gradient (horizon/zenith lerp) or physically-based sky (Preetham)
- Simple version: lerp blue→white based on ray.y in `miss.slang`
- Files: `miss.slang`

### ImGui controls
**Learns:** UI integration with Vulkan
- Sliders for max_bounces, exposure, NEE toggle
- Already stubbed out but disabled in `main.cpp`
- Files: `main.cpp`, `rt_pipeline.h`

---

## Tier 2 — New material types, ~4-8h each

### Mirror (perfect specular)
**Learns:** specular reflection, material branching in shaders
- `reflect(dir, normal)` — no randomness
- Add material type field; closesthit branches on it
- Files: `types.slang`, `closesthit.slang`, `raygen.slang`, scene material setup

### Glass / dielectric
**Learns:** Fresnel equations, Snell's law, refraction, total internal reflection
- Schlick approximation for Fresnel; `refract()` for transmission
- Shadow rays must skip glass (or handle correctly)
- Most physically interesting material after Lambertian
- Files: `types.slang`, `closesthit.slang`, `raygen.slang`

### Depth of field
**Learns:** thin lens model, aperture/focus
- Sample ray origin on a disk (aperture), aim at focus plane
- Pure camera change, no BRDF involved
- Files: `raygen.slang`, `camera.h`

### HDR environment map
**Learns:** texture sampling in shaders, importance sampling an environment
- Load `.hdr` file (stb_image), upload as VkImage, sample in miss shader by ray direction → spherical UV
- Simple version: just sample without importance sampling
- Files: `miss.slang`, `rt_pipeline.h`, new texture upload code

---

## Tier 3 — More involved, ~20-40h each

### GGX microfacet BRDF
**Learns:** microfacet theory, specular importance sampling, rough metals/dielectrics
- D (GGX normal distribution), G (Smith geometry), F (Fresnel) terms
- Need to importance-sample the GGX distribution (sample visible normals)
- Enables rough metal and rough glass
- Files: `raygen.slang`, `closesthit.slang`, `types.slang`

### glTF loading + textures
**Learns:** scene format, texture coordinates, PBR material pipeline
- Replace tinyobjloader with tinygltf
- Support albedo/roughness/metallic/normal textures
- Requires UV coordinates in Vertex struct
- Files: `scene.h`, `types.slang`, `rt_pipeline.h`

### Normal maps
**Learns:** tangent space, TBN matrix
- Requires UVs and tangents from geometry
- Perturbs shading normal in closesthit
- Best done alongside glTF since .obj has no standardized tangent support

---

## Tier 4 — Research-level

### OIDN denoiser
**Learns:** post-process integration, denoising buffers (albedo, normal AOVs)
- Intel Open Image Denoise — C library, integrates as a post-process pass
- Need to output albedo + normal as separate AOV buffers
- Makes low-sample-count renders usable

### ReSTIR DI
**Learns:** reservoir sampling, temporal/spatial reuse
- Worthwhile once scene has many lights

---

## Suggested order
1. Tone mapping — immediate visual quality improvement, trivial
2. ImGui — makes iterating on everything else much faster
3. Sky gradient — makes scenes feel alive
4. Mirror — first new material, simple
5. Glass — most visually impressive early win
6. Depth of field — fun, pure camera math
7. HDR environment map — opens up non-Cornell scenes
8. GGX BRDF — big jump in realism
9. glTF + textures — enables real scenes
