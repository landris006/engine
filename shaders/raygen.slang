import types;
import constants;
import rand;
import bsdf;
import utils;

[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(1, 0)]] RWTexture2D<float4> output_image;
[[vk::binding(2, 0)]] ConstantBuffer<CameraUbo> camera;
[[vk::binding(4, 0)]] StructuredBuffer<Material> materials;
[[vk::binding(5, 0)]] StructuredBuffer<LightTriangle> light_triangles;
[[vk::binding(7, 0)]] Sampler2D skybox;

[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;

[shader("raygeneration")]
void main() {
  uint2 launch_id  = DispatchRaysIndex().xy;
  uint2 launch_dim = DispatchRaysDimensions().xy;

  uint seed = launch_id.x * 1973u + launch_id.y * 9277u + push_constants.sample_count * 26699u + asuint(push_constants.time) * 104729u;

  // jittered UV
  float2 uv = (float2(launch_id) + float2(rand(seed), rand(seed))) / float2(launch_dim);
  uv.y = 1.0 - uv.y;

  // build ray from camera
  float3 origin = camera.origin.xyz;
  float3 dir = normalize(
    camera.lower_left.xyz +
    uv.x * camera.horizontal.xyz +
    uv.y * camera.vertical.xyz -
    origin);

  float3 radiance   = float3(0, 0, 0);
  float3 throughput = float3(1, 1, 1);

  float3 prev_normal    = float3(0, 0, 1);
  float3 prev_albedo    = float3(0);
  float  prev_metallic  = 0.0;
  float  prev_roughness = 1.0;
  float3 V_prev         = float3(0, 1, 0); // dummy, only used at bounce >= 1

  for (uint bounce = 0; bounce < push_constants.max_bounces; bounce++) {
    HitPayload payload;
    payload.missed = false;
    payload.seed   = seed;

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = 0.001;
    ray.TMax      = 1e9;

    TraceRay(tlas,
             RAY_FLAG_NONE,
             0xFF,
             0,
             1,
             0,
             ray,
             payload);

    seed = payload.seed;

    // --- sky miss ---
    if (payload.missed) {
      float3 sky_color = payload.emissive;
      if (bounce > 0 && push_constants.light_count == 0) {
        // MIS: weight this BRDF-sampled sky hit against the cosine-hemisphere sky NEE pdf
        float pdf_sky_nee = max(dot(dir, prev_normal), 0.0) / PI;
        float pdf_brdf    = pdf_bsdf(prev_albedo, prev_metallic, prev_roughness,
                                     V_prev, dir, prev_normal);
        float w = (pdf_sky_nee > 0.0) ? power_heuristic(pdf_brdf, pdf_sky_nee) : 1.0;
        radiance += throughput * sky_color * w;
      } else {
        radiance += throughput * sky_color;
      }
      break;
    }

    // --- emissive surface hit ---
    if (length(payload.emissive) > 0.0) {
      if (bounce == 0) {
        // Camera ray directly sees the light — add fully.
        radiance += throughput * payload.emissive;
      } else {
        // BRDF-sampled bounce hit an emissive. MIS weight against NEE strategy.
        float cos_surf = max(dot(dir, prev_normal), 0.0);
        float pdf_brdf = pdf_bsdf(prev_albedo, prev_metallic, prev_roughness,
                          V_prev, dir, prev_normal);

        float cos_light = max(dot(-dir, payload.normal), 0.0);
        float pdf_light = 0.0;
        if (cos_light > 0.0 && payload.light_area > 0.0 && push_constants.light_count > 0) {
          pdf_light = (payload.hit_dist * payload.hit_dist) /
                      (cos_light * payload.light_area * float(push_constants.light_count));
        }
        float w_brdf = (pdf_light > 0.0) ? power_heuristic(pdf_brdf, pdf_light) : 1.0;


        radiance += throughput * payload.emissive * w_brdf;
        // Unsimplified form:
        // radiance += throughput * payload.emissive * w_brdf * cos_surf / pdf_brdf / PI;
      }
    }

    // --- NEE: explicit light sampling ---
    if (push_constants.light_count > 0) {
      uint light_idx = uint(rand(seed) * float(push_constants.light_count)) % push_constants.light_count;
      LightTriangle lt = light_triangles[light_idx];

      // Uniform sample on triangle (Shirley 1997)
      float r1 = rand(seed);
      float r2 = rand(seed);
      float sqrt_r1 = sqrt(r1);
      float3 lp = lt.v0 * (1.0 - sqrt_r1) +
                  lt.v1 * (sqrt_r1 * (1.0 - r2)) +
                  lt.v2 * (sqrt_r1 * r2);

      float3 to_light  = lp - payload.next_origin;
      float  dist      = length(to_light);
      float3 light_dir = to_light / dist;

      float cos_surf  = max(dot(payload.normal, light_dir), 0.0);
      float cos_light = max(dot(-light_dir, lt.normal), 0.0);

      if (cos_surf > 0.0 && cos_light > 0.0) {
        ShadowPayload sp;
        sp.occluded = true;
        RayDesc sr;
        sr.Origin    = payload.next_origin;
        sr.Direction = light_dir;
        sr.TMin      = 0.0;
        sr.TMax      = dist - 0.002;
        TraceRay(tlas,
                 0x0Cu,  // ACCEPT_FIRST_HIT_AND_END_SEARCH | SKIP_CLOSEST_HIT_SHADER
                 0xFF, 0, 1, 1,
                 sr, sp);

        if (!sp.occluded) {
          Material lmat   = materials[lt.material_index];
          float pdf_light = (dist * dist) / (cos_light * lt.area * float(push_constants.light_count));

          float3 V_curr       = -dir;
          float3 bsdf_nee     = eval_bsdf(payload.albedo, payload.metallic, payload.roughness,
                                          V_curr, light_dir, payload.normal);
          float  pdf_brdf_nee = pdf_bsdf(payload.albedo, payload.metallic, payload.roughness,
                                         V_curr, light_dir, payload.normal);
          float  w_nee        = power_heuristic(pdf_light, pdf_brdf_nee);
          radiance += throughput * lmat.emissive_factor * bsdf_nee * w_nee / pdf_light;
        }
      }
    }

    // --- NEE: sky sampling (fallback when no explicit lights) ---
    if (push_constants.light_count == 0) {
      float3 sky_dir  = cosine_hemisphere(seed, payload.normal);
      float  cos_surf = max(dot(sky_dir, payload.normal), 0.0);
      float  pdf_sky  = cos_surf / PI;
      if (pdf_sky > 0.0) {
        ShadowPayload sp;
        sp.occluded = true;
        RayDesc sr;
        sr.Origin    = payload.next_origin;
        sr.Direction = sky_dir;
        sr.TMin      = 0.0;
        sr.TMax      = 1e9;
        TraceRay(tlas, 0x0Cu, 0xFF, 0, 1, 1, sr, sp);
        if (!sp.occluded) {
          float3 sky_radiance = sample_skybox(sky_dir, skybox);
          float3 V_curr       = -dir;
          float3 bsdf_val     = eval_bsdf(payload.albedo, payload.metallic, payload.roughness,
                                          V_curr, sky_dir, payload.normal);
          float  pdf_brdf_nee = pdf_bsdf(payload.albedo, payload.metallic, payload.roughness,
                                         V_curr, sky_dir, payload.normal);
          float  w_nee        = power_heuristic(pdf_sky, pdf_brdf_nee);
          radiance += throughput * sky_radiance * bsdf_val * w_nee / pdf_sky;
        }
      }
    }

    float3 V = -dir;
    float3 brdf_weight;
    dir = sample_bsdf(seed, V, payload.normal,
                      payload.albedo, payload.metallic, payload.roughness,
                      brdf_weight);

    throughput *= brdf_weight;

    // --- Russian roulette ---
    if (bounce >= 2) {
      float p = max(throughput.x, max(throughput.y, throughput.z));
      if (rand(seed) > p) { 
        break;
      }
      throughput /= p;
    }

    // --- next bounce ---

    prev_albedo    = payload.albedo;
    prev_metallic  = payload.metallic;
    prev_roughness = payload.roughness;
    prev_normal    = payload.normal;
    V_prev         = V;
    origin         = payload.next_origin;
  }

  // running average accumulation
  float4 prev = output_image[launch_id];
  float t = 1.0 / float(push_constants.sample_count);

  output_image[launch_id] = lerp(prev, float4(radiance, 1.0), t); // prev * (n-1)/n + new * 1/n
}
