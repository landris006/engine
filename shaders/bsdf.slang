import constants;
import rand;

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


float ggx_D(float NdotH, float alpha2) {
  float d = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
  return alpha2 / (PI * d * d);
}

// Accounts for microfacets being hidden by neighboring microfacets (from one direction).
float ggx_G1(float NdotV, float alpha2) {
  float tan2 = (1.0 - NdotV * NdotV) / max(NdotV * NdotV, 1e-6);
  return 2.0 / (1.0 + sqrt(1.0 + alpha2 * tan2));
}

// Models both masking (toward light) and shadowing (toward viewer) together.
// More accurate than multiplying two G1 terms.
float ggx_G2(float NdotV, float NdotL, float alpha2) {
  float tan2V = (1.0 - NdotV * NdotV) / max(NdotV * NdotV, 1e-6);
  float tan2L = (1.0 - NdotL * NdotL) / max(NdotL * NdotL, 1e-6);
  return 2.0 / (sqrt(1.0 + alpha2 * tan2V) + sqrt(1.0 + alpha2 * tan2L));
}


// Reflectance increases at grazing angles. `F0` is the reflectance at normal incidence.
//
// - Dielectric (plastic, wood, stone): `F0 = float3(0.04)` — about 4%
// - Metal: `F0 = albedo` — the albedo *is* the specular tint
// - Blend: `F0 = lerp(float3(0.04), albedo, metallic)`
float3 fresnel_schlick(float3 F0, float VdotH) {
  return F0 + (1.0 - F0) * pow(max(1.0 - VdotH, 0.0), 5.0);
}


// Given light direction `L` and view direction `V` (both pointing *away* from the surface),
// returns `bsdf * NdotL`. Includes `NdotL` so callers don't multiply it separately.
//
// - `(1 - F)` in the diffuse term: energy not reflected specularly goes into diffuse.
// - `(1 - metallic)` kills diffuse for metals (no subsurface scattering).
float3 eval_bsdf(float3 albedo, float metallic, float roughness,
                 float3 V, float3 L, float3 N) {
  float NdotV = max(dot(N, V), 1e-4);
  float NdotL = max(dot(N, L), 0.0);
  if (NdotL <= 0.0) {
    return float3(0.0);
  }

  float3 H    = normalize(V + L);
  float NdotH = max(dot(N, H), 0.0);
  float VdotH = max(dot(V, H), 0.0);

  float  alpha  = roughness * roughness;
  float  alpha2 = alpha * alpha;

  float3 F0     = lerp(float3(0.04), albedo, metallic);
  float3 F      = fresnel_schlick(F0, VdotH);

  float  D      = ggx_D(NdotH, alpha2);
  float  G      = ggx_G2(NdotV, NdotL, alpha2);

  float3 specular = D * G * F / (4.0 * NdotV);
  float3 diffuse  = albedo * (1.0 - metallic) * (1.0 - F) / PI;

  return (diffuse + specular) * NdotL;
}


// With probability `split_prob`, sample the specular lobe; otherwise sample diffuse.
// Use Fresnel at normal incidence as a proxy for how "specular" the material is.
float split_prob(float3 albedo, float metallic) {
  float3 F0 = lerp(float3(0.04), albedo, metallic);
  return clamp(max(F0.r, max(F0.g, F0.b)), 0.05, 0.95);
}

// Instead of sampling the full NDF (which wastes samples on microfacets not visible from `V`),
// sample only *visible* normals. This dramatically reduces variance at grazing angles.
float3 sample_vndf(inout uint seed, float3 V, float3 N, float alpha) {
  // Build tangent frame around N
  float3 up = abs(N.x) < 0.999 ? float3(1,0,0) : float3(0,1,0);
  float3 T  = normalize(cross(up, N));
  float3 B  = cross(N, T);

  // Transform V to local frame, stretch by alpha
  float3 Vh = normalize(float3(dot(V,T), dot(V,B), dot(V,N)) * float3(alpha, alpha, 1.0));

  // Orthonormal basis around Vh
  float  lensq = Vh.x*Vh.x + Vh.y*Vh.y;
  float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1,0,0);
  float3 T2 = cross(Vh, T1);

  // Sample point on hemisphere cap visible from Vh
  float r   = sqrt(rand(seed));
  float phi = 2.0 * PI * rand(seed);
  float t1  = r * cos(phi);
  float t2  = r * sin(phi);
  float s   = 0.5 * (1.0 + Vh.z);
  t2 = (1.0 - s) * sqrt(1.0 - t1*t1) + s * t2;

  // Combine and un-stretch back to ellipsoid
  float3 Nh = t1*T1 + t2*T2 + sqrt(max(0.0, 1.0 - t1*t1 - t2*t2)) * Vh;
  float3 Ne = normalize(float3(alpha*Nh.x, alpha*Nh.y, max(0.0, Nh.z)));

  // Back to world space — Ne is the sampled half-vector H
  return T*Ne.x + B*Ne.y + N*Ne.z;
}

float pdf_diffuse(float NdotL) {
  return max(NdotL, 0.0) / PI;
}

float pdf_specular(float3 V, float3 L, float3 N, float alpha) {
  float alpha2 = alpha * alpha;
  float NdotV  = max(dot(N, V), 1e-4);
  float3 H     = normalize(V + L);
  float NdotH  = max(dot(N, H), 0.0);
  float D  = ggx_D(NdotH, alpha2);
  float G1 = ggx_G1(NdotV, alpha2);
  // VNDF PDF: p(L) = D * G1 / (4 * NdotV)
  return D * G1 / (4.0 * NdotV);
}

float pdf_bsdf(float3 albedo, float metallic, float roughness,
               float3 V, float3 L, float3 N) {
  float sp    = split_prob(albedo, metallic);
  float alpha = roughness * roughness;
  float NdotL = max(dot(N, L), 0.0);
  return sp * pdf_specular(V, L, N, alpha) + (1.0 - sp) * pdf_diffuse(NdotL);
}

float3 sample_bsdf(inout uint seed,
                   float3 V, float3 N,
                   float3 albedo, float metallic, float roughness,
                   out float3 weight) {
  float sp    = split_prob(albedo, metallic);
  float alpha = roughness * roughness;

  float3 L;
  if (rand(seed) < sp) {
    // Specular lobe: sample VNDF, reflect V around H
    float3 H = sample_vndf(seed, V, N, alpha);
    L = reflect(-V, H);
    if (dot(L, N) <= 0.0) {
      // Degenerate (can happen near horizon at high roughness) — fall back
      L = cosine_hemisphere(seed, N);
    }
  } else {
    // Diffuse lobe
    L = cosine_hemisphere(seed, N);
  }

  float NdotL = max(dot(N, L), 0.0);
  if (NdotL <= 0.0) { 
    weight = float3(0);
    return L;
  }

  float3 bsdf = eval_bsdf(albedo, metallic, roughness, V, L, N);
  float  pdf  = pdf_bsdf(albedo, metallic, roughness, V, L, N);

  weight = pdf > 0.0 ? bsdf / pdf : float3(0);
  return L;
}
