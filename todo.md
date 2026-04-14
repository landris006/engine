# TODO

## Auto-exposure
- 3 compute shaders:
  - luminance histogram
  - average & adapt
  - apply (write exposure value to 1x1 texture, read this in the tonemapping step)

## Glass / dielectric
- Schlick approximation for Fresnel; `refract()` for transmission
- Shadow rays must skip glass (or handle correctly)

## Depth of field
- Sample ray origin on a disk (aperture), aim at focus plane
- only needs changes in the camera

## Fix lights
- 1 NEE sample for 10k+ lights does nothing
- AS maybe (mentioned in Ray Tracing Gems by Nvidia)

## OIDN denoiser
- Intel Open Image Denoise — C library, integrate as a post-process pass

## ReSTIR DI
- big undertaking, but DI is probably manageable, not sure about GI
