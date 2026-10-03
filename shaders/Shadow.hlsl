static const uint FLAG_BASE_COLOR_TEXTURE = 1u << 1u;

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};

cbuffer ShadowFrameConstants : register(b0)
{
    column_major float4x4 light_view_projection;
};

cbuffer DrawConstants : register(b1)
{
    column_major float4x4 world;
    column_major float4x4 normal_matrix;
    float4 base_color_factor;
    float4 emissive_metallic;
    float4 roughness_normal_ao_cutoff;
    uint flags;
    uint material_diagnostic_id;
    uint alpha_mode;
    uint base_color_texcoord;
    uint metallic_roughness_texcoord;
    uint normal_texcoord;
    uint occlusion_texcoord;
    uint emissive_texcoord;
    float world_handedness;
    uint draw_padding0;
    uint draw_padding1;
    uint draw_padding2;
};

Texture2D<float4> base_color_texture : register(t0);
SamplerState base_color_sampler : register(s0);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(light_view_projection, mul(world, float4(input.position, 1.0)));
    output.uv0 = input.uv0;
    output.uv1 = input.uv1;
    return output;
}

void PSMain(VertexOutput input)
{
    if (alpha_mode != 1u)
        return;
    const float texture_alpha = (flags & FLAG_BASE_COLOR_TEXTURE) != 0u
        ? base_color_texture.Sample(base_color_sampler, base_color_texcoord == 1u ? input.uv1 : input.uv0).a
        : 1.0;
    if (base_color_factor.a * texture_alpha < roughness_normal_ao_cutoff.w)
        discard;
}
