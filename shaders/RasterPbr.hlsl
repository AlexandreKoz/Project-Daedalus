#include "common/PbrLighting.hlsli"

static const uint FLAG_VERTEX_COLOR = 1u << 0u;
static const uint FLAG_BASE_COLOR_TEXTURE = 1u << 1u;
static const uint FLAG_METALLIC_ROUGHNESS_TEXTURE = 1u << 2u;
static const uint FLAG_NORMAL_TEXTURE = 1u << 3u;
static const uint FLAG_OCCLUSION_TEXTURE = 1u << 4u;
static const uint FLAG_EMISSIVE_TEXTURE = 1u << 5u;
static const uint FLAG_DOUBLE_SIDED = 1u << 6u;
static const uint FLAG_HAS_TANGENTS = 1u << 7u;
static const uint FLAG_BOUNDS_OVERLAY = 1u << 8u;

static const uint DIAGNOSTIC_SHADED = 0u;
static const uint DIAGNOSTIC_NORMALS = 1u;
static const uint DIAGNOSTIC_UV = 2u;
static const uint DIAGNOSTIC_TANGENTS = 3u;
static const uint DIAGNOSTIC_BASE_COLOR = 5u;
static const uint DIAGNOSTIC_METALLIC = 6u;
static const uint DIAGNOSTIC_ROUGHNESS = 7u;
static const uint DIAGNOSTIC_EMISSIVE = 8u;
static const uint DIAGNOSTIC_MATERIAL_ID = 9u;

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
    float3 world_position : POSITION0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color : COLOR0;
};

struct GpuLight
{
    float4 position_range;
    float4 direction_outer_cos;
    float4 color_intensity;
    uint type;
    float inner_cone_cos;
    uint padding0;
    uint padding1;
};

cbuffer FrameConstants : register(b0)
{
    column_major float4x4 view_projection;
    float4 camera_position;
    float4 provisional_ambient;
    uint diagnostic_mode;
    uint light_count;
    uint frame_padding0;
    uint frame_padding1;
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
};

cbuffer LightConstants : register(b2)
{
    GpuLight lights[32];
};

Texture2D<float4> base_color_texture : register(t0);
Texture2D<float4> metallic_roughness_texture : register(t1);
Texture2D<float4> normal_texture : register(t2);
Texture2D<float4> occlusion_texture : register(t3);
Texture2D<float4> emissive_texture : register(t4);
SamplerState base_color_sampler : register(s0);
SamplerState metallic_roughness_sampler : register(s1);
SamplerState normal_sampler : register(s2);
SamplerState occlusion_sampler : register(s3);
SamplerState emissive_sampler : register(s4);

float2 select_uv(uint set_index, float2 uv0, float2 uv1)
{
    return set_index == 1u ? uv1 : uv0;
}

float3 material_id_color(uint identifier)
{
    if (identifier == 0u)
        return float3(1.0, 0.0, 1.0); // canonical default material sentinel
    uint hash = identifier * 747796405u + 2891336453u;
    hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
    hash = (hash >> 22u) ^ hash;
    return float3(
        0.2 + 0.8 * float((hash >> 0u) & 255u) / 255.0,
        0.2 + 0.8 * float((hash >> 8u) & 255u) / 255.0,
        0.2 + 0.8 * float((hash >> 16u) & 255u) / 255.0);
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float4 world_position4 = mul(world, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position4);
    output.world_position = world_position4.xyz / max(abs(world_position4.w), 1.0e-20);
    output.normal = mul((float3x3)normal_matrix, input.normal);
    output.tangent.xyz = mul((float3x3)world, input.tangent.xyz);
    output.tangent.w = input.tangent.w * world_handedness;
    output.uv0 = input.uv0;
    output.uv1 = input.uv1;
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input, bool is_front_face : SV_IsFrontFace) : SV_Target0
{
    if ((flags & FLAG_BOUNDS_OVERLAY) != 0u)
        return float4(1.0, 0.8, 0.1, 1.0);

    const float4 base_texture_sample = (flags & FLAG_BASE_COLOR_TEXTURE) != 0u
        ? base_color_texture.Sample(base_color_sampler, select_uv(base_color_texcoord, input.uv0, input.uv1))
        : 1.0.xxxx;
    const float4 vertex_color = (flags & FLAG_VERTEX_COLOR) != 0u ? input.color : 1.0.xxxx;
    const float4 base_color = base_color_factor * base_texture_sample * vertex_color;

    if (alpha_mode == 1u && base_color.a < roughness_normal_ao_cutoff.w)
        discard;

    const float4 metallic_roughness_sample = (flags & FLAG_METALLIC_ROUGHNESS_TEXTURE) != 0u
        ? metallic_roughness_texture.Sample(metallic_roughness_sampler,
                                            select_uv(metallic_roughness_texcoord, input.uv0, input.uv1))
        : 1.0.xxxx;
    const float roughness = saturate(roughness_normal_ao_cutoff.x * metallic_roughness_sample.g);
    const float metallic = saturate(emissive_metallic.w * metallic_roughness_sample.b);

    const float3 emissive_sample = (flags & FLAG_EMISSIVE_TEXTURE) != 0u
        ? emissive_texture.Sample(emissive_sampler, select_uv(emissive_texcoord, input.uv0, input.uv1)).rgb
        : 1.0.xxx;
    const float3 emissive = emissive_metallic.xyz * emissive_sample;

    const float ao_sample = (flags & FLAG_OCCLUSION_TEXTURE) != 0u
        ? occlusion_texture.Sample(occlusion_sampler, select_uv(occlusion_texcoord, input.uv0, input.uv1)).r
        : 1.0;
    const float ambient_occlusion = lerp(1.0, ao_sample, saturate(roughness_normal_ao_cutoff.z));

    const float face_sign = ((flags & FLAG_DOUBLE_SIDED) != 0u && !is_front_face) ? -1.0 : 1.0;
    float3 geometric_normal = input.normal;
    const float normal_length_squared = dot(geometric_normal, geometric_normal);
    geometric_normal = normal_length_squared > 1.0e-20 ? geometric_normal * rsqrt(normal_length_squared) : float3(0.0, 0.0, 1.0);
    geometric_normal *= face_sign;

    // UV tangent directions are surface derivatives and do not reverse merely because the
    // fragment is viewed from the back. Reverse N and the handedness sign instead; that keeps
    // T/B aligned with the authored UV parameterization while reversing the shading normal.
    float3 tangent = input.tangent.xyz;
    float tangent_sign = input.tangent.w * face_sign;
    tangent -= geometric_normal * dot(geometric_normal, tangent);
    const float tangent_length_squared = dot(tangent, tangent);
    const bool tangent_valid = tangent_length_squared > 1.0e-20;
    if (tangent_valid)
        tangent *= rsqrt(tangent_length_squared);

    float3 shading_normal = geometric_normal;
    if ((flags & FLAG_NORMAL_TEXTURE) != 0u && (flags & FLAG_HAS_TANGENTS) != 0u && tangent_valid)
    {
        float3 tangent_normal = normal_texture.Sample(normal_sampler, select_uv(normal_texcoord, input.uv0, input.uv1)).xyz * 2.0 - 1.0;
        tangent_normal.xy *= roughness_normal_ao_cutoff.y;
        const float tangent_normal_length_squared = dot(tangent_normal, tangent_normal);
        if (tangent_normal_length_squared > 1.0e-20)
        {
            tangent_normal *= rsqrt(tangent_normal_length_squared);
            const float3 bitangent = normalize(cross(geometric_normal, tangent)) * tangent_sign;
            const float3 mapped = tangent * tangent_normal.x + bitangent * tangent_normal.y + geometric_normal * tangent_normal.z;
            const float mapped_length_squared = dot(mapped, mapped);
            if (mapped_length_squared > 1.0e-20)
                shading_normal = mapped * rsqrt(mapped_length_squared);
        }
    }

    if (diagnostic_mode == DIAGNOSTIC_NORMALS)
        return float4(shading_normal * 0.5 + 0.5, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_UV)
        return float4(frac(select_uv(base_color_texcoord, input.uv0, input.uv1)), 0.0, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_TANGENTS)
    {
        const float3 tangent_visual = tangent_valid ? tangent * 0.5 + 0.5 : float3(1.0, 0.0, 1.0);
        return float4(tangent_visual * (tangent_sign < 0.0 ? 0.35 : 1.0), 1.0);
    }
    if (diagnostic_mode == DIAGNOSTIC_BASE_COLOR)
        return float4(base_color.rgb, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_METALLIC)
        return float4(metallic.xxx, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_ROUGHNESS)
        return float4(roughness.xxx, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_EMISSIVE)
        return float4(emissive, 1.0);
    if (diagnostic_mode == DIAGNOSTIC_MATERIAL_ID)
        return float4(material_id_color(material_diagnostic_id), 1.0);

    const float3 view_delta = camera_position.xyz - input.world_position;
    const float view_length_squared = dot(view_delta, view_delta);
    const float3 view_direction = view_length_squared > 1.0e-20 ? view_delta * rsqrt(view_length_squared) : shading_normal;
    float3 direct = 0.0;

    [loop]
    for (uint index = 0u; index < min(light_count, 32u); ++index)
    {
        const GpuLight light = lights[index];
        float3 light_direction = 0.0;
        float attenuation = 1.0;
        if (light.type == 0u)
        {
            light_direction = -light.direction_outer_cos.xyz;
            const float len2 = dot(light_direction, light_direction);
            if (len2 <= 1.0e-20) continue;
            light_direction *= rsqrt(len2);
        }
        else
        {
            const float3 to_light = light.position_range.xyz - input.world_position;
            const float distance_squared = dot(to_light, to_light);
            if (distance_squared <= 1.0e-20) continue;
            const float distance_to_light = sqrt(distance_squared);
            light_direction = to_light / distance_to_light;
            attenuation = distance_attenuation(distance_to_light, light.position_range.w);
            if (light.type == 2u)
            {
                const float3 emitted_direction = normalize(light.direction_outer_cos.xyz);
                const float cos_angle = dot(emitted_direction, -light_direction);
                attenuation *= spot_attenuation(cos_angle, light.inner_cone_cos, light.direction_outer_cos.w);
            }
        }
        if (attenuation <= 0.0) continue;
        const float3 incident = light.color_intensity.rgb * (light.color_intensity.w * attenuation);
        direct += evaluate_brdf_times_n_dot_l(base_color.rgb, metallic, roughness,
                                               shading_normal, view_direction, light_direction) * incident;
    }

    // C1 has no IBL. AO modulates only this explicit provisional indirect diffuse term;
    // direct punctual lighting is intentionally unaffected. C2 replaces this term with IBL.
    const float3 provisional_indirect = provisional_ambient.rgb * base_color.rgb * (1.0 - metallic) * ambient_occlusion;
    float3 hdr_color = direct + provisional_indirect + emissive;
    const float output_alpha = alpha_mode == 2u ? saturate(base_color.a) : 1.0;
    // The scene target is R16G16B16A16_FLOAT. Make arithmetic failures conspicuous rather
    // than storing NaN/Inf, and clamp only finite values that exceed the target's representable
    // maximum. This is a storage-format boundary, not tone-map concealment.
    if (any(isnan(hdr_color)) || any(isinf(hdr_color)))
        hdr_color = float3(65504.0, 0.0, 65504.0);
    else
        hdr_color = min(max(hdr_color, 0.0), 65504.0.xxx);
    return float4(hdr_color, output_alpha);
}
