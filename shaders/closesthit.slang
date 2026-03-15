import types;

[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;

[[vk::binding(3, 0)]] StructuredBuffer<MeshInfo> mesh_infos;
[[vk::binding(4, 0)]] StructuredBuffer<Material> materials;
[[vk::binding(8, 0)]] Sampler2D[] textures;

[shader("closesthit")]
void main(inout HitPayload payload, float2 attribs : SV_IntersectionAttributes) {
  MeshInfo mesh = mesh_infos[InstanceID()];

  Vertex* vertex_buffer = (Vertex*)mesh.vertex_address;
  uint*   vertex_index = (uint*)mesh.index_address;

  uint base = PrimitiveIndex() * 3;
  Vertex v0 = vertex_buffer[vertex_index[base + 0]];
  Vertex v1 = vertex_buffer[vertex_index[base + 1]];
  Vertex v2 = vertex_buffer[vertex_index[base + 2]];

  // barycentrics
  float b0 = 1.0 - attribs.x - attribs.y;
  float b1 = attribs.x;
  float b2 = attribs.y;

  float3 pos = b0 * v0.pos + b1 * v1.pos + b2 * v2.pos;
  float3 local_normal = normalize(b0 * v0.normal + b1 * v1.normal + b2 * v2.normal);
  float2 uv = b0 * v0.uv + b1 * v1.uv + b2 * v2.uv;
  float4 tangent = b0 * v0.tangent + b1 * v1.tangent + b2 * v2.tangent;

  Material mat = materials[mesh.material_index];

  float3x3 obj_to_world = (float3x3)ObjectToWorld3x4();
  float3 world_normal = normalize(mul(obj_to_world, local_normal));
  float3 world_tang   = normalize(mul(obj_to_world, tangent.xyz));

  float4 base_color = mat.base_color_factor;
  if (mat.base_color_tex >= 0) {
    base_color *= textures[NonUniformResourceIndex(mat.base_color_tex)].SampleLevel(uv, 0);
  }

  float metallic  = mat.metallic_factor;
  float roughness = mat.roughness_factor;
  if (mat.metal_rough_tex >= 0) {
    float4 mr = textures[NonUniformResourceIndex(mat.metal_rough_tex)].SampleLevel(uv, 0);
    roughness *= mr.g;
    metallic  *= mr.b;
  }

  float3 world_pos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

  float3 normal = world_normal;
  if (mat.normal_tex >= 0) {
    // tangent space normal
    float4 normal_sample = textures[NonUniformResourceIndex(mat.normal_tex)].SampleLevel(uv, 0);
    float2 xy = normal_sample.xy * 2.0 - 1.0;  // [0,1] -> [-1,1]
    float3 normal_ts = float3(xy, sqrt(max(0.0, 1.0 - dot(xy, xy))));
    float3 bitangent = cross(world_normal, world_tang) * tangent.w;
    // transform normal from tangent space to world space
    normal = normalize(normal_ts.x * world_tang + normal_ts.y * bitangent + normal_ts.z * world_normal);
  }

  float3 emissive = mat.emissive_factor;
  if (mat.emissive_tex >= 0) {
    emissive *= textures[NonUniformResourceIndex(mat.emissive_tex)].SampleLevel(uv, 0).rgb;
  }

  payload.albedo      = base_color.xyz;
  payload.metallic    = metallic;
  payload.roughness   = max(roughness, 0.04);
  payload.emissive    = emissive;
  payload.normal      = normal;
  payload.next_origin = world_pos + world_normal * 0.001;
  payload.hit_dist    = RayTCurrent();

  if (length(mat.emissive_factor) > 0.0) {
    float3 e1 = v1.pos - v0.pos;
    float3 e2 = v2.pos - v0.pos;
    payload.light_area = length(cross(e1, e2)) * 0.5;
  } else {
    payload.light_area = 0.0;
  }
}
