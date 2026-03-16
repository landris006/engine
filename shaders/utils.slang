import types;
import constants;

float3 sample_skybox(float3 dir, Sampler2D skybox) {
  float phi   = atan2(dir.z, dir.x);
  float theta = acos(clamp(dir.y, -1.0f, 1.0f));
  float u = (phi + PI) / (2.0f * PI);
  float v = theta / PI;

  float3 color = skybox.SampleLevel(float2(u, v), 0.0f).rgb;

  // tone down if too bright
  float lum = dot(color, float3(0.2126, 0.7152, 0.0722)); // ITU-R BT.709 luma coefficients
  const float max_lum = 5.0;
  if (lum > max_lum) {
    color *= max_lum / lum;
  }

  return color;
}
