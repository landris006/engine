[[vk::binding(1, 0)]] RWTexture2D<float4> in_image;
[[vk::binding(6, 0)]] RWTexture2D<float4> out_image;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 globalThreadID: SV_DispatchThreadID)
{
  uint2 imageSize;
  in_image.GetDimensions(imageSize.x, imageSize.y);

  // Check if the thread is within the image bounds
  if(any(globalThreadID >= imageSize)) {
    return;
  }

  float4 color = in_image[globalThreadID.xy];
  float3 rgb = color.rgb;

  static const float exposure = 1.0;
  rgb *= exposure;

  // rgb = rgb / (rgb + float4(1.0)); // Reinhard
  rgb = (rgb * (2.51 * rgb + 0.03)) / (rgb * (2.43 * rgb + 0.59) + 0.14);  // ACES

  rgb = pow(rgb, 1.0 / 2.2);  // gamma correction

  out_image[globalThreadID.xy] = float4(rgb, color.a);
}
