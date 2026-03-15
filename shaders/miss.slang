import types;
import constants;
import utils;

[[vk::binding(7, 0)]] Sampler2D skybox;

[shader("miss")]
void main(inout HitPayload payload) {
  float3 dir = normalize(WorldRayDirection());

  payload.emissive = sample_skybox(dir, skybox);
  payload.missed   = true;
}
