import types;

[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(1, 0)]] RWTexture2D<float4> output_image;
[[vk::binding(2, 0)]] ConstantBuffer<CameraUbo> camera;

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
  float phi = 2.0 * 3.14159265 * r2;
  float3 h = float3(r * cos(phi), r * sin(phi), sqrt(1.0 - r1));
  // build ONB around n
  float3 up = abs(n.x) < 0.999 ? float3(1, 0, 0) : float3(0, 1, 0);
  float3 tangent = normalize(cross(up, n));
  float3 bitangent = cross(n, tangent);
  return normalize(tangent * h.x + bitangent * h.y + n * h.z);
}


[shader("raygeneration")]
void main() {
  uint2 launch_id  = DispatchRaysIndex().xy;
  uint2 launch_dim = DispatchRaysDimensions().xy;

  uint seed = launch_id.x * 1973u + launch_id.y * 9277u + push_constants.sample_count * 26699u;

  // jittered UV
  float2 uv = (float2(launch_id) + float2(rand(seed), rand(seed))) / float2(launch_dim);

  // build ray from camera
  float3 origin = camera.origin.xyz;
  float3 dir = normalize(
    camera.lower_left.xyz +
    uv.x * camera.horizontal.xyz +
    uv.y * camera.vertical.xyz -
    origin);

  float3 radiance   = float3(0, 0, 0);
  float3 throughput = float3(1, 1, 1);

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
             0xFF,           // instance mask
             0,              // hit group offset
             1,              // geometry stride
             0,              // miss shader index (primary miss)
             ray,
             payload);

    seed = payload.seed;

    if (payload.missed) {
      break;
    }

    radiance  += throughput * payload.emissive;
    throughput *= payload.albedo;

    // Russian roulette after bounce 2
    if (bounce >= 2) {
      float p = max(throughput.x, max(throughput.y, throughput.z));
      if (rand(seed) > p) {
        break;
      }
      throughput /= p;
    }

    // next bounce
    dir    = cosine_hemisphere(seed, payload.normal);
    origin = payload.next_origin;
  }

  // running average accumulation
  float4 prev = output_image[launch_id];
  float t = 1.0 / float(push_constants.sample_count);
  output_image[launch_id] = lerp(prev, float4(radiance, 1.0), t); // prev * (n-1)/n + new/n
}
