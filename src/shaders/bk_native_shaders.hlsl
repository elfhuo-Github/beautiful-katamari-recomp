// beautiful_katamari - native renderer shaders (HLSL).
//
// Native shaders for the BK native renderer, compiled to SPIR-V (Vulkan)
// or used directly (D3D12). These are BK-specific rewrites of the game's
// own Xenos shaders; the concrete layouts must be reverse engineered from
// the guest's shader constant bank usage.
//
// Placeholder: the actual shaders are TBD pending reverse engineering of
// BK's pixel/vertex shader programs.

struct VSInput {
  float3 pos : POSITION;
  float3 normal : NORMAL;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
  // Blend attributes (skinned meshes):
  uint4 blend_indices : BLENDINDICES;
  float4 blend_weights : BLENDWEIGHT;
};

struct VSOutput {
  float4 pos : SV_POSITION;
  float3 normal : NORMAL;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
  float4 world_pos : TEXCOORD1;
};

cbuffer CB_Vertex : register(b0) {
  float4x4 g_world;
  float4x4 g_view_proj;
  float4x4 g_view;
  float4x4 g_proj;
  float4 g_cam_pos;
  float4 g_fog; // ramp scale/bias/exponent, transmittance
  float4 g_bones[256]; // bone palette (3 rows per bone)
};

cbuffer CB_Pixel : register(b1) {
  float4 g_diffuse;
  float4 g_fog_color;
  float4 g_ambient;
  float4 g_sun_dir;
  float4 g_exposure;
};

Texture2D g_diffuse_tex : register(t0);
Texture2D g_lightmap_tex : register(t1);
SamplerState g_sampler : register(s0);

VSOutput VSMain(VSInput input) {
  VSOutput output;
  float4 pos = float4(input.pos, 1.0f);

  if (input.blend_weights.x > 0.0f) {
    // Skinning: weighted bone palette
    float4 skinned_pos = 0;
    float4 skinned_normal = 0;
    float total_weight = 0;
    // Unroll max 4 influences:
    #if 0
    for (uint i = 0; i < 4; i++) {
      uint bone = input.blend_indices[i];
      float w = input.blend_weights[i];
      if (w > 0.0f) {
        float4x4 bone_mat = float4x4(
          g_bones[bone*3+0], g_bones[bone*3+1], g_bones[bone*3+2],
          float4(0,0,0,1));
        skinned_pos += mul(pos, bone_mat) * w;
        skinned_normal += mul(float4(input.normal, 0), bone_mat) * w;
        total_weight += w;
      }
    }
    if (total_weight > 0.0f) {
      skinned_pos /= total_weight;
      skinned_normal /= total_weight;
    }
    pos = skinned_pos;
    output.normal = skinned_normal;
    #endif
  } else {
    pos = mul(pos, g_world);
    output.normal = mul(float4(input.normal, 0), g_world).xyz;
  }

  output.pos = mul(pos, g_view_proj);
  output.uv = input.uv;
  output.color = input.color;
  output.world_pos = pos;
  return output;
}

float4 PSMain(VSOutput input) : SV_Target {
  float4 diff = g_diffuse;
  // Sample diffuse texture:
  // diff *= g_diffuse_tex.Sample(g_sampler, input.uv);
  // Sample lightmap:
  // float4 lm = g_lightmap_tex.Sample(g_sampler, input.uv);
  // diff *= lm;
  // Fog:
  // float fog = saturate(distance(input.world_pos.xyz, g_cam_pos.xyz) * g_fog.x + g_fog.y);
  // diff = lerp(diff, g_fog_color, fog);
  return diff;
}
