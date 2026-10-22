#ifndef DAEDALUS_SAMPLING_HLSLI
#define DAEDALUS_SAMPLING_HLSLI

#include "common/PbrLighting.hlsli"

// GPU twin of src/rendering/PathTracingMath.{h,cpp}. Every formula here must stay identical to the
// CPU reference so portable tests continue to describe the shader behaviour.

uint pcg_hash(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Jarzynski & Olano 2020.
uint4 pcg4d(uint4 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    v ^= v >> 16u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    return v;
}

struct PathRng
{
    uint state;
};

// Stream key: (pixel, global sample index, seed). Independent of frame scheduling and frame slots.
PathRng make_path_rng(uint2 pixel, uint sample_index, uint seed)
{
    const uint4 hashed = pcg4d(uint4(pixel.x, pixel.y, sample_index, seed));
    PathRng rng;
    rng.state = hashed.x ^ (hashed.y * 0x9E3779B9u);
    return rng;
}

uint rng_next_uint(inout PathRng rng)
{
    rng.state = rng.state * 747796405u + 2891336453u;
    const uint word = ((rng.state >> ((rng.state >> 28u) + 4u)) ^ rng.state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rng_next_float(inout PathRng rng)
{
    return float(rng_next_uint(rng) >> 8u) * (1.0 / 16777216.0);
}

float2 rng_next_float2(inout PathRng rng)
{
    const float x = rng_next_float(rng);
    const float y = rng_next_float(rng);
    return float2(x, y);
}

// Branchless orthonormal basis (Duff et al. 2017).
void orthonormal_basis(float3 n, out float3 tangent, out float3 bitangent)
{
    const float sign_z = n.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (sign_z + n.z);
    const float b = n.x * n.y * a;
    tangent = float3(1.0 + sign_z * n.x * n.x * a, sign_z * b, -sign_z * n.x);
    bitangent = float3(b, sign_z + n.y * n.y * a, -n.y);
}

float3 to_world(float3 local, float3 tangent, float3 bitangent, float3 normal)
{
    return tangent * local.x + bitangent * local.y + normal * local.z;
}

float3 to_local(float3 world, float3 tangent, float3 bitangent, float3 normal)
{
    return float3(dot(world, tangent), dot(world, bitangent), dot(world, normal));
}

float3 sample_cosine_hemisphere(float2 u)
{
    const float radius = sqrt(max(u.x, 0.0));
    const float phi = 2.0 * DAEDALUS_PI * u.y;
    return float3(radius * cos(phi), radius * sin(phi), sqrt(max(1.0 - u.x, 0.0)));
}

float smith_g1_ggx(float n_dot_v, float alpha)
{
    if (n_dot_v <= 0.0)
        return 0.0;
    const float alpha_squared = alpha * alpha;
    return 2.0 * n_dot_v / (n_dot_v + sqrt(alpha_squared + (1.0 - alpha_squared) * n_dot_v * n_dot_v));
}

float ggx_d(float n_dot_h, float alpha)
{
    return n_dot_h <= 0.0 ? 0.0 : ggx_distribution(n_dot_h, alpha);
}

// Heitz 2018 visible-normal sampling; v_local in the shading frame (+Z = normal).
float3 sample_ggx_vndf(float3 v_local, float alpha, float2 u)
{
    const float3 vh = normalize(float3(alpha * v_local.x, alpha * v_local.y, v_local.z));
    const float length_squared = vh.x * vh.x + vh.y * vh.y;
    const float3 t1 = length_squared > 0.0 ? float3(-vh.y, vh.x, 0.0) * rsqrt(length_squared) : float3(1.0, 0.0, 0.0);
    const float3 t2 = cross(vh, t1);
    const float radius = sqrt(max(u.x, 0.0));
    const float phi = 2.0 * DAEDALUS_PI * u.y;
    const float p1 = radius * cos(phi);
    float p2 = radius * sin(phi);
    const float s = 0.5 * (1.0 + vh.z);
    p2 = (1.0 - s) * sqrt(max(1.0 - p1 * p1, 0.0)) + s * p2;
    const float3 nh = t1 * p1 + t2 * p2 + vh * sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2));
    return normalize(float3(alpha * nh.x, alpha * nh.y, max(0.0, nh.z)));
}

// Solid-angle pdf of the VNDF-reflected direction: G1(v) D(h) / (4 n.v).
float ggx_vndf_reflection_pdf(float n_dot_v, float n_dot_h, float alpha)
{
    if (n_dot_v <= 0.0 || n_dot_h <= 0.0)
        return 0.0;
    return smith_g1_ggx(n_dot_v, alpha) * ggx_d(n_dot_h, alpha) / (4.0 * n_dot_v);
}

float power_heuristic(float pdf_a, float pdf_b)
{
    const float a = pdf_a * pdf_a;
    const float b = pdf_b * pdf_b;
    return (a + b) > 0.0 ? a / (a + b) : (pdf_a > 0.0 ? 1.0 : 0.0);
}

float fresnel_dielectric(float cos_incident, float eta)
{
    cos_incident = saturate(cos_incident);
    const float sin_t_squared = (1.0 - cos_incident * cos_incident) / (eta * eta);
    if (sin_t_squared >= 1.0)
        return 1.0;
    const float cos_t = sqrt(1.0 - sin_t_squared);
    const float rs = (cos_incident - eta * cos_t) / (cos_incident + eta * cos_t);
    const float rp = (eta * cos_incident - cos_t) / (eta * cos_incident + cos_t);
    return 0.5 * (rs * rs + rp * rp);
}

float3 sample_triangle_barycentrics(float2 u)
{
    const float root = sqrt(max(u.x, 0.0));
    const float b0 = 1.0 - root;
    const float b1 = u.y * root;
    return float3(b0, b1, 1.0 - b0 - b1);
}

// Mixture importance sampler for daedalus-procedural-sky-v1 (see PathTracingMath.h):
// p(w) = q * (n + 1)/(2 pi) * max(cos(w, sun), 0)^n + (1 - q)/(4 pi).
float3 sample_environment_direction(float3 sun_axis, float exponent, float sun_probability, float3 u)
{
    const float phi = 2.0 * DAEDALUS_PI * u.z;
    if (u.x < sun_probability)
    {
        const float cos_theta = pow(max(u.y, 1.0e-30), 1.0 / (exponent + 1.0));
        const float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
        float3 tangent;
        float3 bitangent;
        orthonormal_basis(sun_axis, tangent, bitangent);
        return normalize(tangent * (sin_theta * cos(phi)) + bitangent * (sin_theta * sin(phi)) + sun_axis * cos_theta);
    }
    const float z = 1.0 - 2.0 * u.y;
    const float r = sqrt(max(0.0, 1.0 - z * z));
    return float3(r * cos(phi), r * sin(phi), z);
}

float environment_direction_pdf(float3 sun_axis, float exponent, float sun_probability, float3 direction)
{
    const float cos_sun = dot(sun_axis, direction);
    const float lobe = cos_sun > 0.0 ? (exponent + 1.0) / (2.0 * DAEDALUS_PI) * pow(cos_sun, exponent) : 0.0;
    return sun_probability * lobe + (1.0 - sun_probability) / (4.0 * DAEDALUS_PI);
}

float luminance(float3 value)
{
    return dot(value, float3(0.2126, 0.7152, 0.0722));
}

bool all_finite(float3 value)
{
    return !any(isnan(value)) && !any(isinf(value));
}

#endif
