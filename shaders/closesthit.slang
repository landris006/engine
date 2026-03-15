import types;

[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;

[[vk::binding(3, 0)]] StructuredBuffer<MeshInfo> mesh_infos;
[[vk::binding(4, 0)]] StructuredBuffer<Material> materials;

[shader("closesthit")]
void main(inout HitPayload payload, float2 attribs : SV_IntersectionAttributes) {
  MeshInfo mesh = mesh_infos[InstanceIndex()];

  Vertex* vertex_buffer = (Vertex*)mesh.vertex_address;
  uint*   vertex_index = (uint*)mesh.index_address;

  uint base = PrimitiveIndex() * 3;
  Vertex v0 = vertex_buffer[vertex_index[base + 0]];
  Vertex v1 = vertex_buffer[vertex_index[base + 1]];
  Vertex v2 = vertex_buffer[vertex_index[base + 2]];

  // barycentrics: attribs.x = b1, attribs.y = b2, b0 = 1 - b1 - b2
  float b0 = 1.0 - attribs.x - attribs.y;
  float3 pos    = b0 * v0.pos    + attribs.x * v1.pos    + attribs.y * v2.pos;
  float3 normal = normalize(b0 * v0.normal + attribs.x * v1.normal + attribs.y * v2.normal);

  Material mat = materials[mesh.material_index];

  float3x3 obj_to_world = (float3x3)ObjectToWorld3x4();
  float3 world_normal = normalize(mul(obj_to_world, normal));

  float3 world_pos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

  payload.albedo      = mat.base_color_factor.xyz;
  payload.emissive    = mat.emissive_factor;
  payload.normal      = world_normal;
  payload.next_origin = world_pos + world_normal * 0.001;
  payload.hit_dist    = RayTCurrent();
  payload.metallic    = mat.metallic_factor;
  payload.roughness   = mat.roughness_factor;

  if (length(mat.emissive_factor) > 0.0) {
    float3 e1 = v1.pos - v0.pos;
    float3 e2 = v2.pos - v0.pos;
    payload.light_area = length(cross(e1, e2)) * 0.5;
  } else {
    payload.light_area = 0.0;
  }
}
