import types;

[shader("miss")]
void main(inout HitPayload payload) {
  float3 dir = normalize(WorldRayDirection());
  float t = 0.5 * (dir.y + 1.0);
  payload.emissive = lerp(float3(1.0, 1.0, 1.0), float3(0.4, 0.6, 1.0), t);
  payload.missed = true;
}
