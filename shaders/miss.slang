import types;
import constants;

[[vk::binding(7, 0)]] Sampler2D skybox;

[shader("miss")]
void main(inout HitPayload payload) {
  float3 dir = normalize(WorldRayDirection());
  float phi = atan2(dir.z, dir.x);
  float theta = acos(clamp(dir.y, -1.0f, 1.0f));
  float u = (phi + PI) / (2.0f * PI);
  float v = theta / PI;

  payload.emissive = skybox.SampleLevel(float2(u, v), 0.0f).rgb;
  payload.missed   = true;
}
