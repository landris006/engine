# TODO

## Auto-exposure
- Luminance Histogram (Compute Pass 1): A compute shader reads your HDR in_image and tallies up the brightness of every pixel into a histogram buffer. We use a histogram instead of a simple average so a few ultra-bright pixels (like looking directly at the sun) don't plunge the rest of the screen into pitch blackness.
- Average & Adapt (Compute Pass 2): A tiny 1-thread compute shader reads that histogram, calculates the average brightness (ignoring the extreme highest and lowest peaks), and smoothly interpolates it with the previous frame's brightness so the transition happens gradually over time.
- Apply: This pass writes the final exposure value to a 1x1 texture or an SSBO, which you bind to your current tonemapping shader. You then swap static const float exposure = 1.0; for float exposure = exposure_buffer[0];.

## Glass / dielectric
**Learns:** Fresnel equations, Snell's law, refraction, total internal reflection
- Schlick approximation for Fresnel; `refract()` for transmission
- Shadow rays must skip glass (or handle correctly)
- Most physically interesting material after Lambertian
- Files: `types.slang`, `closesthit.slang`, `raygen.slang`

## Depth of field
**Learns:** thin lens model, aperture/focus
- Sample ray origin on a disk (aperture), aim at focus plane
- Pure camera change, no BRDF involved
- Files: `raygen.slang`, `camera.h`

## Fix lights
- 1 NEE sample for 10k+ lights does nothing
- AS maybe

## OIDN denoiser
**Learns:** post-process integration, denoising buffers (albedo, normal AOVs)
- Intel Open Image Denoise — C library, integrates as a post-process pass
- Need to output albedo + normal as separate AOV buffers
- Makes low-sample-count renders usable

## ReSTIR DI
**Learns:** reservoir sampling, temporal/spatial reuse
- Worthwhile once scene has many lights
