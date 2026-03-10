import types;

[shader("miss")]
void main(inout HitPayload payload) {
  payload.missed = true;
}
