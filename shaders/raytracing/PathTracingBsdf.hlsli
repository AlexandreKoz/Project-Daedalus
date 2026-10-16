#ifndef DAEDALUS_PATH_TRACING_BSDF_HLSLI
#define DAEDALUS_PATH_TRACING_BSDF_HLSLI

// Campaign E reference BSDF (declared model).
//
// glTF 2.0 metallic-roughness core, following the specification's reference composition:
//   f = (1 - m) * dielectric + m * metal
//   metal       = F_schlick(baseColor, v.h) * D * V
//   dielectric  = mix(diffuse, specular, F_schlick(f0_ior, v.h)) for the opaque fraction (1 - t),
//                 with diffuse = baseColor / pi and specular = D * V (height-correlated Smith GGX).
// KHR_materials_transmission (factor only) adds a dielectric INTERFACE lobe with weight (1 - m) * t:
//   * thickness > 0 (KHR_materials_volume): rough refractive interface (Walter et al. 2007) with the
//     exact unpolarized Fresnel term, total internal reflection, and Beer-Lambert absorption from
//     attenuationColor/attenuationDistance applied by the integrator while inside the medium;
//   * thickness == 0: thin-walled interface - the transmitted direction is the reflected direction
//     mirrored through the tangent plane (no bending), as the glTF thin-walled model prescribes.
// Base colour tints the transmitted lobe. Alpha BLEND is NOT transmission: it is stochastic
// coverage in the any-hit shader. Raster/hybrid paths do not implement transmission (documented).
//
// Sampling: one-sample mixture over {specular reflection, diffuse, interface}. The returned pdf is
// always the full mixture solid-angle pdf so BSDF/light MIS weights are consistent.

#include "raytracing/RayTracingCommon.hlsli"

struct BsdfContext
{
    float3 normal;          // shading normal on the incident (view) side
    float3 geometric_normal; // geometric normal on the incident side
    float3 tangent;
    float3 bitangent;
    float3 view;            // unit direction toward the previous vertex
    float3 base_color;
    float metallic;
    float alpha;            // GGX alpha = perceptual roughness^2 (clamped)
    float transmission;
    float eta;              // n_transmitted / n_incident across this interface
    float f0_dielectric;
    bool volume;            // refractive interface (true) or thin-walled (false)
    float probability_specular;
    float probability_diffuse;
    float probability_interface;
};

struct BsdfSample
{
    float3 direction;
    float3 weight;          // f * |n.l| / pdf
    float pdf;
    bool transmitted;
    bool total_internal_reflection;
    bool valid;
};

float schlick_scalar(float f0, float v_dot_h)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - v_dot_h), 5.0);
}

BsdfContext make_bsdf_context(SurfaceHit hit, MaterialSample material, float3 view, bool entering)
{
    BsdfContext context;
    context.geometric_normal = hit.geometric_normal;
    float3 normal = hit.shading_normal;
    if (dot(normal, context.geometric_normal) < 0.0)
        normal = -normal;
    // Shading normals that face away from the viewer produce invalid microfacet frames; fall back
    // to the geometric normal for this vertex (defensible, energy-safe choice).
    if (dot(normal, view) <= 1.0e-4)
        normal = context.geometric_normal;
    context.normal = normal;
    orthonormal_basis(normal, context.tangent, context.bitangent);
    context.view = view;
    context.base_color = material.base_color;
    context.metallic = material.metallic;
    const float roughness = max(material.roughness, DAEDALUS_MIN_PERCEPTUAL_ROUGHNESS);
    context.alpha = roughness * roughness;
    context.transmission = material.transmission;
    context.volume = material.thickness > 0.0;
    const float ior = max(material.ior, 1.0);
    context.eta = context.volume ? (entering ? ior : 1.0 / ior) : ior;
    const float ratio = (ior - 1.0) / (ior + 1.0);
    context.f0_dielectric = ratio * ratio;

    const float n_dot_v = saturate(dot(normal, view));
    const float fresnel_dielectric_view = schlick_scalar(context.f0_dielectric, n_dot_v);
    const float3 fresnel_metal_view = fresnel_schlick(context.base_color, n_dot_v);
    const float base_luminance = luminance(context.base_color);
    const float opaque = (1.0 - context.metallic) * (1.0 - context.transmission);
    float weight_specular = (1.0 - context.metallic) * (1.0 - context.transmission) * fresnel_dielectric_view +
                            context.metallic * luminance(fresnel_metal_view);
    const float weight_diffuse = opaque * (1.0 - fresnel_dielectric_view) * base_luminance;
    const float weight_interface = (1.0 - context.metallic) * context.transmission * max(base_luminance, 0.05);
    weight_specular = max(weight_specular, 0.05 * (weight_specular + weight_diffuse + weight_interface) + 1.0e-4);
    const float total = weight_specular + weight_diffuse + weight_interface;
    context.probability_specular = weight_specular / total;
    context.probability_diffuse = weight_diffuse / total;
    context.probability_interface = weight_interface / total;
    return context;
}

// Microfacet reflection terms shared by the specular and interface lobes.
float reflection_d_vis(BsdfContext context, float3 light, float3 half_vector, out float pdf_vndf)
{
    const float n_dot_v = dot(context.normal, context.view);
    const float n_dot_l = dot(context.normal, light);
    const float n_dot_h = dot(context.normal, half_vector);
    pdf_vndf = ggx_vndf_reflection_pdf(n_dot_v, n_dot_h, context.alpha);
    if (n_dot_v <= 0.0 || n_dot_l <= 0.0 || n_dot_h <= 0.0)
        return 0.0;
    return ggx_d(n_dot_h, context.alpha) * smith_correlated_visibility(n_dot_v, n_dot_l, context.alpha);
}

// Evaluates f(v, l) * |n.l| for the full BSDF and returns the mixture pdf of sampling `light`.
float3 bsdf_evaluate(BsdfContext context, float3 light, out float pdf)
{
    pdf = 0.0;
    const float n_dot_v = dot(context.normal, context.view);
    const float n_dot_l = dot(context.normal, light);
    if (n_dot_v <= 0.0)
        return 0.0;
    float3 value = 0.0;
    if (n_dot_l > 0.0 && dot(context.geometric_normal, light) > 0.0)
    {
        const float3 half_vector = normalize(context.view + light);
        const float v_dot_h = saturate(dot(context.view, half_vector));
        float pdf_reflection = 0.0;
        const float d_vis = reflection_d_vis(context, light, half_vector, pdf_reflection);
        const float fresnel_dielectric_vh = schlick_scalar(context.f0_dielectric, v_dot_h);
        const float3 fresnel_metal_vh = fresnel_schlick(context.base_color, v_dot_h);
        const float opaque = (1.0 - context.metallic) * (1.0 - context.transmission);
        const float3 specular = (opaque * fresnel_dielectric_vh + context.metallic * fresnel_metal_vh) * d_vis;
        const float3 diffuse = opaque * (1.0 - fresnel_dielectric_vh) * context.base_color / DAEDALUS_PI;
        value += (specular + diffuse) * n_dot_l;
        pdf += context.probability_specular * pdf_reflection + context.probability_diffuse * n_dot_l / DAEDALUS_PI;
        if (context.transmission > 0.0)
        {
            // Interface reflection (exact Fresnel for refractive volumes, Schlick for thin walls).
            const float fresnel_interface = context.volume ? fresnel_dielectric(v_dot_h, context.eta) : fresnel_dielectric_vh;
            value += (1.0 - context.metallic) * context.transmission * fresnel_interface * d_vis * n_dot_l;
            pdf += context.probability_interface * pdf_reflection * fresnel_interface;
        }
    }
    else if (n_dot_l < 0.0 && dot(context.geometric_normal, light) < 0.0 && context.transmission > 0.0)
    {
        const float weight = (1.0 - context.metallic) * context.transmission;
        if (!context.volume)
        {
            // Thin-walled: mirror the transmitted direction back to the reflection hemisphere.
            const float3 mirrored = light - 2.0 * context.normal * dot(context.normal, light);
            const float3 half_vector = normalize(context.view + mirrored);
            const float v_dot_h = saturate(dot(context.view, half_vector));
            float pdf_reflection = 0.0;
            const float d_vis = reflection_d_vis(context, mirrored, half_vector, pdf_reflection);
            const float fresnel = schlick_scalar(context.f0_dielectric, v_dot_h);
            value += weight * (1.0 - fresnel) * context.base_color * d_vis * (-n_dot_l);
            pdf += context.probability_interface * pdf_reflection * (1.0 - fresnel);
        }
        else
        {
            // Walter et al. 2007 refraction. Generalized half vector h = -(v + eta * l), oriented to n.
            float3 half_vector = -(context.view + context.eta * light);
            const float half_length_squared = dot(half_vector, half_vector);
            if (half_length_squared > 1.0e-20)
            {
                half_vector *= rsqrt(half_length_squared);
                if (dot(half_vector, context.normal) < 0.0)
                    half_vector = -half_vector;
                const float v_dot_h = dot(context.view, half_vector);
                const float l_dot_h = dot(light, half_vector);
                if (v_dot_h > 0.0 && l_dot_h < 0.0)
                {
                    const float n_dot_h = dot(context.normal, half_vector);
                    const float fresnel = fresnel_dielectric(v_dot_h, context.eta);
                    const float d = ggx_d(n_dot_h, context.alpha);
                    const float g2 = smith_correlated_visibility(n_dot_v, -n_dot_l, context.alpha) * 4.0 * n_dot_v * (-n_dot_l);
                    const float denominator = v_dot_h + context.eta * l_dot_h;
                    const float denominator_squared = max(denominator * denominator, 1.0e-20);
                    // Half-vector -> direction Jacobian |dw_h/dw_l| = eta^2 |l.h| / (v.h + eta l.h)^2 (pdf only).
                    const float jacobian = context.eta * context.eta * abs(l_dot_h) / denominator_squared;
                    // Walter et al. 2007 eq. 21 for radiance toward v, times |n.l|:
                    // f_t |n.l| = (1 - F) D G2 |v.h| |l.h| / (|n.v| (v.h + eta l.h)^2); the implied
                    // 1/eta^2 radiance scaling across the interface emerges from f/pdf.
                    value += weight * context.base_color * (1.0 - fresnel) * d * g2 * v_dot_h * abs(l_dot_h) /
                             (n_dot_v * denominator_squared);
                    const float pdf_visible_normal = smith_g1_ggx(n_dot_v, context.alpha) * v_dot_h * d / n_dot_v;
                    pdf += context.probability_interface * pdf_visible_normal * jacobian * (1.0 - fresnel);
                }
            }
        }
    }
    return value;
}

BsdfSample bsdf_sample(BsdfContext context, inout PathRng rng)
{
    BsdfSample result;
    result.direction = context.normal;
    result.weight = 0.0;
    result.pdf = 0.0;
    result.transmitted = false;
    result.total_internal_reflection = false;
    result.valid = false;
    const float lobe = rng_next_float(rng);
    const float2 u = rng_next_float2(rng);
    const float u_fresnel = rng_next_float(rng);
    const float3 view_local = to_local(context.view, context.tangent, context.bitangent, context.normal);
    if (view_local.z <= 0.0)
        return result;

    if (lobe < context.probability_diffuse)
    {
        result.direction = to_world(sample_cosine_hemisphere(u), context.tangent, context.bitangent, context.normal);
    }
    else
    {
        const float3 half_local = sample_ggx_vndf(view_local, context.alpha, u);
        const float3 half_vector = to_world(half_local, context.tangent, context.bitangent, context.normal);
        const float3 reflected = reflect(-context.view, half_vector);
        const bool interface_lobe = lobe >= context.probability_diffuse + context.probability_specular;
        if (!interface_lobe)
        {
            result.direction = reflected;
        }
        else
        {
            const float v_dot_h = saturate(dot(context.view, half_vector));
            const float fresnel = context.volume ? fresnel_dielectric(v_dot_h, context.eta)
                                                 : schlick_scalar(context.f0_dielectric, v_dot_h);
            if (u_fresnel < fresnel)
            {
                result.direction = reflected;
                result.total_internal_reflection = context.volume && fresnel >= 1.0;
            }
            else if (!context.volume)
            {
                result.direction = reflected - 2.0 * context.normal * dot(context.normal, reflected);
                result.transmitted = true;
            }
            else
            {
                const float3 refracted = refract(-context.view, half_vector, 1.0 / context.eta);
                if (dot(refracted, refracted) <= 0.0)
                {
                    result.direction = reflected;
                    result.total_internal_reflection = true;
                }
                else
                {
                    result.direction = normalize(refracted);
                    result.transmitted = true;
                }
            }
        }
    }

    float pdf = 0.0;
    const float3 value = bsdf_evaluate(context, result.direction, pdf);
    if (!(pdf > 0.0) || isnan(pdf) || isinf(pdf))
        return result;
    result.pdf = pdf;
    result.weight = value / pdf;
    result.transmitted = dot(result.direction, context.geometric_normal) < 0.0;
    result.valid = any(result.weight > 0.0);
    return result;
}

#endif
