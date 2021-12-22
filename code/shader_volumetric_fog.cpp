#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#define MATH_GLSL 1
#include "../libs/math/math.h"

#include "shader_scene_structs.h"
#include "shader_volumetric_fog.h"

// TODO: Move to math
f32 HenryGreensteinPhaseFunction(f32 LDotV)
{
    f32 GSquare = VolFogBuffer.GScattering * VolFogBuffer.GScattering;
    f32 Result = (1.0f - GSquare) / (4.0f * Pi32 * pow(1.0f + GSquare - 2.0f * VolFogBuffer.GScattering * LDotV, 1.5f));
    return Result;
}

// TODO: Move to math
v4 ProjectAndSampleTexture(sampler2D Texture, m4 Transform, v3 Pos)
{
    v4 TextureSpacePos = Transform * V4(Pos, 1);
    TextureSpacePos.xyz /= TextureSpacePos.w;
    v2 Uv = 0.5f * (TextureSpacePos.xy + V2(1));
    v4 Result = texture(Texture, Uv);

    return Result;
}

//=========================================================================================================================================
// NOTE: Ray March Volumetric Fog
//=========================================================================================================================================

#if RAYMARCH_FOG_FRAG

layout(location = 0) in v2 InUv;

layout(location = 0) out v4 OutColor;

void main()
{
    v3 BackgroundColor = subpassLoad(Color).rgb;
    f32 BackgroundDepth = subpassLoad(Depth).x;
    
    // NOTE: Reconstruct world pos from depth
    v3 WorldPos = V3(0);
    {
        v4 ProjectedPos = V4(2.0f * InUv - V2(1.0f), BackgroundDepth, 1.0f);
        ProjectedPos = SceneBuffer.InvVpTransform * ProjectedPos;
        WorldPos = ProjectedPos.xyz / ProjectedPos.w;
    }

    /*
      NOTE: Lighting Equation:

      - http://cg.iit.bme.hu/~szirmay/lightshaft.pdf
      - https://www.slideshare.net/BenjaminGlatzel/volumetric-lighting-for-many-lights-in-lords-of-the-fallen
    */

    // NOTE: We start from the fragment and move towards the camera
    v3 StartPos = SceneBuffer.CameraPos;
    v3 EndPos = WorldPos;
    v3 RayVector = EndPos - StartPos;
    f32 RayLength = Length(RayVector);
    v3 RayDir = RayVector / RayLength;

    // NOTE: Clamp our distance (we get back corruption since its a infinite fog thats constant)
    RayLength = Min(RayLength, 10.0f);
    EndPos = StartPos + RayVector * RayLength;
    
    u32 NumSteps = 256;
    f32 StepLength = RayLength / NumSteps;
    v3 Step = RayDir * StepLength;

    v3 CurrPos = StartPos;
    v3 L0 = BackgroundColor;
    v3 StartFog = L0 * Exp(-RayLength * VolFogBuffer.Density);
    
    // NOTE: Directional light only fog
#if 0
    f32 AccumFog = 0.0f;
    for (f32 CurrRayLength = 0; CurrRayLength < RayLength - StepLength; CurrRayLength += StepLength)
    {
        // NOTE: Get shadow value
        float ShadowValue = 0.0f;
        {
            v4 ShadowSpacePos = DirectionalLight.VPTransform * V4(CurrPos, 1);
            ShadowSpacePos.xyz /= ShadowSpacePos.w;
            v2 ShadowUv = 0.5f * (ShadowSpacePos.xy + V2(1));
            f32 ShadowSample = texture(Shadow, ShadowUv).x;
            ShadowValue = step(ShadowSample, ShadowSpacePos.z);
        }

        f32 LDotV = Dot(-RayDir, DirectionalLight.Dir);
        // NOTE: We attenuate the light only by distance from its start scatter pos to the camera
        f32 DistanceAttenuation = Exp(-VolFogBuffer.Density * CurrRayLength);
        f32 Phase = HenryGreensteinPhaseFunction(LDotV);
        f32 CurrFog = DistanceAttenuation * ShadowValue * Phase * StepLength;
        AccumFog += CurrFog;
        
        CurrPos += Step;
    }

    v3 FogColor = VolFogBuffer.Density * VolFogBuffer.Albedo * DirectionalLight.Color * AccumFog + StartFog;
#endif

    // NOTE: Directional Light + Point Light Fog
#if 1
    v3 AccumFogColor = V3(0.0f);
    f32 DirectionalLDotV = Dot(-RayDir, DirectionalLight.Dir);
    f32 DirectionalPhase = HenryGreensteinPhaseFunction(DirectionalLDotV);
    
    for (f32 CurrRayLength = 0; CurrRayLength < RayLength - StepLength; CurrRayLength += StepLength)
    {
        v3 CurrFogColor = V3(0.0f);
        
        // NOTE: Accum directional light
        {
            // NOTE: Get shadow value
            float ShadowValue = 0.0f;
            {
                v4 ShadowSpacePos = DirectionalLight.VPTransform * V4(CurrPos, 1);
                ShadowSpacePos.xyz /= ShadowSpacePos.w;
                v2 ShadowUv = 0.5f * (ShadowSpacePos.xy + V2(1));
                f32 ShadowSample = texture(Shadow, ShadowUv).x;
                ShadowValue = step(ShadowSample, ShadowSpacePos.z);
            }

            // NOTE: We attenuate the light only by distance from its start scatter pos to the camera
            f32 DirectionalFogFactor = ShadowValue * DirectionalPhase;
            CurrFogColor += DirectionalFogFactor * DirectionalLight.Color;
        }

        // NOTE: Accum point lights
        for (u32 PointLightId = 0; PointLightId < SceneBuffer.NumPointLights; ++PointLightId)
        {
            point_light CurrLight = PointLights[PointLightId];

            v3 PointLightDir = Normalize(CurrPos - CurrLight.Pos);
            // TODO: For occlussion info here, we would need shadow maps for the light, or we ray march the depth buffer to find if
            // we are occluded
            f32 PointLightLDotV = Dot(-RayDir, PointLightDir);
            f32 Phase = HenryGreensteinPhaseFunction(PointLightLDotV);
            // NOTE: We attenuate our point light color by distance to light
            v3 PointLightColor = CurrLight.Color * PointLightAttenuate(CurrPos, CurrLight);
            CurrFogColor += PointLightColor * Phase;
        }

        // NOTE: Multiply by constants not dependant on the light
        f32 DistanceAttenuation = Exp(-VolFogBuffer.Density * CurrRayLength);
        CurrFogColor *= DistanceAttenuation * StepLength;
        
        AccumFogColor += CurrFogColor;
        CurrPos += Step;
    }

    v3 FogColor = VolFogBuffer.Density * VolFogBuffer.Albedo * AccumFogColor + StartFog;
#endif    
    
    // NOTE: Gamma correct our color
    FogColor = Pow(FogColor, V3(1.0f / 2.2f));
    
    OutColor = V4(FogColor, 1);
}

#endif

//=========================================================================================================================================
// NOTE: Ray March 3D Volumetric Fog
//=========================================================================================================================================

#if RAYMARCH_FOG_3D_FRAG

layout(location = 0) in v2 InUv;

layout(location = 0) out v4 OutColor;

void main()
{
    v3 BackgroundColor = subpassLoad(Color).rgb;
    f32 BackgroundDepth = subpassLoad(Depth).x;
    
    // NOTE: Reconstruct world pos from depth
    v3 WorldPos = V3(0);
    {
        v4 ProjectedPos = V4(2.0f * InUv - V2(1.0f), BackgroundDepth, 1.0f);
        ProjectedPos = SceneBuffer.InvVpTransform * ProjectedPos;
        WorldPos = ProjectedPos.xyz / ProjectedPos.w;
    }

    /*
      NOTE: Lighting Equation:

      - http://cg.iit.bme.hu/~szirmay/lightshaft.pdf
      - https://www.slideshare.net/BenjaminGlatzel/volumetric-lighting-for-many-lights-in-lords-of-the-fallen
    */

    // NOTE: We start from the fragment and move towards the camera
    v3 RayStartPos = SceneBuffer.CameraPos;
    v3 RayEndPos = WorldPos;
    f32 RayOriginalLength = Length(RayEndPos - RayStartPos);
    v3 RayDir = (RayEndPos - RayStartPos) / RayOriginalLength;

    // NOTE: Intersect our ray with the volumetric fog texture to find our start/end pos
    f32 TMin = 0.0f;
    f32 TMax = 0.0f;
    b32 Intersected = false;
    {
        // NOTE: https://tavianator.com/2011/ray_box.html
        v3 TMinVec = (VolFogBuffer.FogMinPos - RayStartPos) / RayDir;
        v3 TMaxVec = (VolFogBuffer.FogMaxPos - RayStartPos) / RayDir;

        TMin = Min(TMinVec.x, TMaxVec.x);
        TMax = Max(TMinVec.x, TMaxVec.x);

        TMin = Max(TMin, Min(TMinVec.y, TMaxVec.y));
        TMax = Min(TMax, Max(TMinVec.y, TMaxVec.y));

        TMin = Max(TMin, Min(TMinVec.z, TMaxVec.z));
        TMax = Min(TMax, Max(TMinVec.z, TMaxVec.z));

        // NOTE: We don't want to look for intersections in the back
        TMin = Max(TMin, 0);
        Intersected = TMax >= TMin;
    }

    if (!Intersected)
    {
        // NOTE: Exit out early
        OutColor = V4(BackgroundColor, 1);
        return;
    }

    // NOTE: Make sure we don't march past our depth buffer
    TMax = Min(TMax, RayOriginalLength);
    
    // NOTE: Calculate the distance we will march
    v3 StartPos = RayStartPos + RayDir * TMin;
    f32 RayLength = TMax - TMin;

    // TODO: We get some aliasing even with 256 step size, dither + blur?
    // TODO: We will step 128 even if we cover a small dist, use a min step size
    // TODO: Zooming out seems to make the fog disappear?
    u32 NumSteps = 64;
    f32 StepLength = RayLength / NumSteps;
    v3 Step = RayDir * StepLength;

    // NOTE: Directional Light + Point Light Fog
    v3 CurrPos = RayStartPos;
    v3 AccumFogColor = V3(0.0f);
    f32 AccumTransmittance = 0.0f;
    f32 DirectionalLDotV = Dot(-RayDir, DirectionalLight.Dir);
    f32 DirectionalPhase = HenryGreensteinPhaseFunction(DirectionalLDotV);
    for (f32 CurrRayLength = 0; CurrRayLength < RayLength - StepLength; CurrRayLength += StepLength)
    {
        v3 CurrFogColor = V3(0.0f);
        f32 CurrDensity = 0.0f;
        {
            // TODO: Calculate the step so we don't do a div every iteration
            v3 FogUv = (CurrPos - VolFogBuffer.FogMinPos) / (VolFogBuffer.FogMaxPos - VolFogBuffer.FogMinPos);
            CurrDensity = texture(FogTextureFiltered, FogUv).x;
        }
        
        // NOTE: Accum directional light
        {
            // NOTE: Get shadow value
            float ShadowValue = 0.0f;
            {
                v4 ShadowSpacePos = DirectionalLight.VPTransform * V4(CurrPos, 1);
                ShadowSpacePos.xyz /= ShadowSpacePos.w;
                v2 ShadowUv = 0.5f * (ShadowSpacePos.xy + V2(1));
                f32 ShadowSample = texture(Shadow, ShadowUv).x;
                ShadowValue = step(ShadowSample, ShadowSpacePos.z);
            }

            // NOTE: We attenuate the light only by distance from its start scatter pos to the camera
            f32 DirectionalFogFactor = ShadowValue * DirectionalPhase;
            CurrFogColor += DirectionalFogFactor * DirectionalLight.Color;
        }

        // NOTE: Accum point lights
        for (u32 PointLightId = 0; PointLightId < SceneBuffer.NumPointLights; ++PointLightId)
        {
            point_light CurrLight = PointLights[PointLightId];

            v3 PointLightDir = Normalize(CurrPos - CurrLight.Pos);
            // TODO: For occlussion info here, we would need shadow maps for the light, or we ray march the depth buffer to find if
            // we are occluded
            f32 PointLightLDotV = Dot(-RayDir, PointLightDir);
            f32 Phase = HenryGreensteinPhaseFunction(PointLightLDotV);
            // NOTE: We attenuate our point light color by distance to light
            v3 PointLightColor = CurrLight.Color * PointLightAttenuate(CurrPos, CurrLight);
            CurrFogColor += PointLightColor * Phase;
        }

        // NOTE: Multiply by constants not dependant on the light
        f32 DistanceAttenuation = Exp(-AccumTransmittance);
        CurrFogColor *= CurrDensity * DistanceAttenuation * StepLength;
        
        AccumTransmittance += CurrDensity * StepLength;
        AccumFogColor += CurrFogColor;
        CurrPos += Step;
    }

    v3 StartFog = BackgroundColor * Exp(-AccumTransmittance);
    
    v3 FogColor = VolFogBuffer.Albedo * AccumFogColor + StartFog;
    
    // NOTE: Gamma correct our color
    FogColor = Pow(FogColor, V3(1.0f / 2.2f));
    
    OutColor = V4(FogColor, 1);
}

#endif
