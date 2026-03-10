import types;

[shader("miss")]
void main(inout ShadowPayload payload) {
  payload.occluded = false;
}
