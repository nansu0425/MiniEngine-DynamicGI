#ifndef __LOCAL_LIGHT_REFLECTION_HLSLI__
#define __LOCAL_LIGHT_REFLECTION_HLSLI__

#include "Core/Shaders/GltfBrdf.hlsli"
#include "Core/Shaders/SceneLights.hlsli"
#include "Core/Shaders/LocalLight.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"
#include "Model/Raytracing/SurfaceRay.hlsli"

template<typename Surface>
float3 ReflectLocalLights(Surface surface, GltfBrdfInput brdf, SceneLights lights)
{
    float3 radiance = 0.0;
    for (uint i = 0; i < lights.localLightCount; ++i)
    {
        const LocalLight light = lights.localLights[i];
        const LocalLightIncidence incidence = GetLocalLightIncidence(light, surface.position);
        const float cosine = dot(surface.shadingNormal, incidence.direction);
        if (cosine <= 0.0 || all(incidence.illuminance == 0.0))
            continue;
        RayDesc ray;
        if (!DescribeSurfaceRayToPoint(surface.position, surface.geometryNormal, light.position, ray) || IsOccluded(ray, kCullSingleSidedBackFaces))
            continue;
        radiance += EvaluateGltfBrdf(brdf, incidence.direction) * incidence.illuminance * cosine;
    }
    return radiance;
}

#endif
