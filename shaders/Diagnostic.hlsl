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
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

cbuffer DrawConstants : register(b0)
{
    column_major float4x4 world_view_projection;
    column_major float4x4 world;
    column_major float4x4 normal_matrix;
    float4 base_color_factor;
    uint diagnostic_mode;
    uint has_texture;
    uint use_vertex_color;
    uint texture_coord_set;
    float world_handedness;
    uint padding0;
    uint padding1;
    uint padding2;
};

Texture2D<float4> base_color_texture : register(t0);
SamplerState base_color_sampler : register(s0);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(world_view_projection, float4(input.position, 1.0));
    output.normal = normalize(mul((float3x3)normal_matrix, input.normal));
    output.tangent.xyz = normalize(mul((float3x3)world, input.tangent.xyz));
    output.tangent.w = input.tangent.w * world_handedness;
    output.uv = texture_coord_set == 1 ? input.uv1 : input.uv0;
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    if (diagnostic_mode == 1)
    {
        return float4(input.normal * 0.5 + 0.5, 1.0);
    }
    if (diagnostic_mode == 2)
    {
        return float4(frac(input.uv), 0.0, 1.0);
    }
    if (diagnostic_mode == 3)
    {
        return float4(1.0, 0.8, 0.1, 1.0);
    }
    if (diagnostic_mode == 4)
    {
        const float3 direction = normalize(input.tangent.xyz) * 0.5 + 0.5;
        const float handedness_scale = input.tangent.w < 0.0 ? 0.35 : 1.0;
        return float4(direction * handedness_scale, 1.0);
    }

    float4 texture_value = has_texture != 0 ? base_color_texture.Sample(base_color_sampler, input.uv) : float4(1.0, 1.0, 1.0, 1.0);
    float4 vertex_value = use_vertex_color != 0 ? input.color : float4(1.0, 1.0, 1.0, 1.0);
    const float3 light_direction = normalize(float3(0.35, 0.75, 0.55));
    const float lighting = 0.2 + 0.8 * saturate(dot(normalize(input.normal), light_direction));
    return float4(base_color_factor.rgb * texture_value.rgb * vertex_value.rgb * lighting,
                  base_color_factor.a * texture_value.a * vertex_value.a);
}
