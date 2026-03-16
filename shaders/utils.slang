import types;
import constants;

float3 sample_skybox(float3 dir, Sampler2D skybox) {
  float phi   = atan2(dir.z, dir.x);
  float theta = acos(clamp(dir.y, -1.0f, 1.0f));
  float u = (phi + PI) / (2.0f * PI);
  float v = theta / PI;

  float3 color = skybox.SampleLevel(float2(u, v), 0.0f).rgb;

  return color;
}
