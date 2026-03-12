import types;
import constants;

[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(1, 0)]] RWTexture2D<float4> output_image;
[[vk::binding(2, 0)]] ConstantBuffer<CameraUbo> camera;
[[vk::binding(4, 0)]] StructuredBuffer<Material> materials;
[[vk::binding(5, 0)]] StructuredBuffer<LightTriangle> light_triangles;

[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;

uint pcg(inout uint state) {
  state = state * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

float rand(inout uint seed) {
  return float(pcg(seed)) / float(0xffffffffu);
}

float3 cosine_hemisphere(inout uint seed, float3 n) {
  float r1 = rand(seed);
  float r2 = rand(seed);
  float r = sqrt(r1);
  float phi = 2.0 * PI * r2;
  float3 h = float3(r * cos(phi), r * sin(phi), sqrt(1.0 - r1));
  // build ONB around n
  float3 up = abs(n.x) < 0.999 ? float3(1, 0, 0) : float3(0, 1, 0);
  float3 tangent = normalize(cross(up, n));
  float3 bitangent = cross(n, tangent);
  return normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

float power_heuristic(float pdf_a, float pdf_b) {
  float a2 = pdf_a * pdf_a;
  return a2 / (a2 + pdf_b * pdf_b);
}


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

  float3 prev_normal = float3(0, 0, 1);

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
      radiance += throughput * payload.emissive;
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
        float pdf_brdf = cos_surf / PI;

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
          float pdf_brdf  = cos_surf / PI;
          float w_nee     = power_heuristic(pdf_light, pdf_brdf);
          radiance += throughput * payload.albedo * lmat.emissive *
                      cos_surf * w_nee / pdf_light / PI;
        }
      }
    }

    throughput *= payload.albedo;

    // --- Russian roulette ---
    if (bounce >= 2) {
      float p = max(throughput.x, max(throughput.y, throughput.z));
      if (rand(seed) > p) { 
        break;
      }
      throughput /= p;
    }

    // --- next bounce ---
    prev_normal = payload.normal;
    dir         = cosine_hemisphere(seed, payload.normal);
    origin      = payload.next_origin;
  }

  // running average accumulation
  float4 prev = output_image[launch_id];
  float t = 1.0 / float(push_constants.sample_count);

  output_image[launch_id] = lerp(prev, float4(radiance, 1.0), t); // prev * (n-1)/n + new * 1/n
}
