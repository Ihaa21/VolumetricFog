#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#define MATH_GLSL 1
#include "../libs/math/math.h"

#include "shader_scene_structs.h"

layout(set = 0, binding = 0) uniform scene_buffer          
{
    scene_globals SceneBuffer;
};                                                      
                                                                        
layout(set = 0, binding = 2) buffer point_light_buffer     
{                                                                   
    point_light PointLights[];                                      
};                                                                  
                                                                        
layout(set = 0, binding = 4) buffer directional_light_buffer 
{                                                                   
    directional_light_gpu DirectionalLight;                         
};                                                                  

layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput Color;
layout (input_attachment_index = 0, set = 1, binding = 1) uniform subpassInput Depth;

layout(set = 1, binding = 2) uniform vol_fog_buffer
{
    f32 GScattering; // NOTE: Anisotropy, probability of forward scattering
    f32 Density; // NOTE: Probability of being absorbed
    f32 Albedo; // NOTE: Probability of scattering (in/out)
} VolFogBuffer;

// TODO: Move to math
f32 HenryGreensteinPhaseFunction(f32 LDotV)
{
    f32 GSquare = VolFogBuffer.GScattering * VolFogBuffer.GScattering;
    f32 Result = (1.0f - GSquare) / (4.0f * Pi32 * pow(1.0f + GSquare - 2.0f * VolFogBuffer.GScattering * LDotV, 1.5f));
    return Result;
}

//=========================================================================================================================================
// NOTE: Ray March Volumetric Fog Frag
//=========================================================================================================================================

#if RAYMARCH_SMOKE

layout(location = 0) in v2 InUv;

layout(location = 0) out v4 OutColor;

f32 ABSORPTION_COEFF = 0.5f;
v3 VOLUME_ALBEDO = V3(0.9f);

float hash1( float n )
{
    return fract( n*17.0*fract( n*0.3183099 ) );
}

float noise( in vec3 x )
{
    vec3 p = floor(x);
    vec3 w = fract(x);
    
    vec3 u = w*w*w*(w*(w*6.0-15.0)+10.0);
    
    float n = p.x + 317.0*p.y + 157.0*p.z;
    
    float a = hash1(n+0.0);
    float b = hash1(n+1.0);
    float c = hash1(n+317.0);
    float d = hash1(n+318.0);
    float e = hash1(n+157.0);
    float f = hash1(n+158.0);
    float g = hash1(n+474.0);
    float h = hash1(n+475.0);

    float k0 =   a;
    float k1 =   b - a;
    float k2 =   c - a;
    float k3 =   e - a;
    float k4 =   a - b - c + d;
    float k5 =   a - c - e + g;
    float k6 =   a - b - e + f;
    float k7 = - a + b + c - d + e - f - g + h;

    return -1.0+2.0*(k0 + k1*u.x + k2*u.y + k3*u.z + k4*u.x*u.y + k5*u.y*u.z + k6*u.z*u.x + k7*u.x*u.y*u.z);
}

f32 fbm_4(v3 x)
{
    const mat3 Transform  = mat3( 0.00,  0.80,  0.60,
                                  -0.80,  0.36, -0.48,
                                  -0.60, -0.48,  0.64 );
    
    f32 f = 2.0;
    f32 s = 0.5;
    f32 a = 0.0;
    f32 b = 0.5;
    
    for(u32 i = Min(0, SceneBuffer.CurrFrameId); i < 4; i++)
    {
        f32 n = noise(x);
        a += b*n;
        b *= s;
        x = f*Transform*x;
    }
    return a;
}

f32 SmokeSdf(v3 Pos)
{
    f32 FrameTime = SceneBuffer.CurrFrameTime * 0.5f;

    v3 ScalingFactor = V3(1.0f / 64.0f, 1.0f / 64.0f, 1.0f / 8.0f);

    Pos /= ScalingFactor;
    
    v3 fbmCoord = (Pos + 2.0 * V3(FrameTime, 0.0, FrameTime)) / 1.5f;
    f32 Result = SdfSphere(SdfTranslate(Pos, V3(-8.0, 2.0 + 20.0 * Sin(FrameTime), -1)), 5.6);
    Result = SdfSmoothUnion(Result, SdfSphere(SdfTranslate(Pos, V3(8.0, 8.0 + 12.0 * Cos(FrameTime), 3)), 5.6), 3.0f);
    Result = SdfSmoothUnion(Result, SdfSphere(SdfTranslate(Pos, V3(5.0 * Sin(FrameTime), 3.0, 0)), 8.0), 3.0) + 7.0 * fbm_4(fbmCoord / 3.2);
    Result = SdfSmoothUnion(Result, SdfPlane(Pos + V3(0, 0.4, 0), V3(0, 0, 1)), 22.0);

    Result *= Min(ScalingFactor.x, Min(ScalingFactor.y, ScalingFactor.z));
    
#if 0
    f32 Sphere1 = SdfSphere(SdfTranslate(Pos, V3(0.3f * Cos(FrameTime), 0.2f, 0.2f)), 0.95f);
    f32 Sphere2 = SdfSphere(SdfTranslate(Pos, V3(0.3f, 0.2f, 0.9f * (Sin(FrameTime) + 1.0f))), 0.5f);

    v3 FbmCoord = (Pos + 2.0 * V3(FrameTime, 0.0, FrameTime)) / 1.5f;
    
    f32 Result = SdfSmoothUnion(Sphere1, Sphere2, 0.8f) + 3.5f * fbm_4(FbmCoord / 1.2f);
    // NOTE: Downscale our result
    Result *= 0.25f;
#endif
    
    return Result;
}

f32 GetLightVisibility(v3 RayStart, v3 RayDir, f32 MaxT, u32 MaxSteps, f32 StepLength)
{
    f32 Result = 0.0f;
    f32 T = 0.0f;

    for (u32 StepId = 0; StepId < MaxSteps; ++StepId)
    {
        T += StepLength;
        if (T > MaxT)
        {
            break;
        }

        v3 CurrPos = RayStart + RayDir * T;
        if (SmokeSdf(CurrPos) < 0.0f)
        {
            Result *= Exp(-ABSORPTION_COEFF * StepLength);
        }
    }

    return Result;
}

void main()
{
    v3 BackgroundColor = subpassLoad(Color).rgb;
    f32 BackgroundDepth = subpassLoad(Depth).x;

    // NOTE: Find background world pos
    v3 WorldPos;
    {
        v4 WorldPos4 = (SceneBuffer.InvVpTransform * V4(2.0f * InUv - V2(1), BackgroundDepth, 1));
        WorldPos4.xyz /= WorldPos4.w;
        WorldPos = WorldPos4.xyz;
    }

    // NOTE: We trace in world space
    v3 RayPos = SceneBuffer.CameraPos;
    v3 RayVec = WorldPos - RayPos;
    f32 RayLength = Length(RayVec);
    v3 RayDir = RayVec / RayLength;

    f32 MaxT = Min(8.0f, RayLength);
    
    // NOTE: Trace a ray
    b32 Intersected = false;
    f32 T = 0.0f;
    u32 NumIterations = 0;
    {
        f32 Epsilon = 0.05f;
        u32 MaxIterations = 100;
        for (NumIterations = 0; NumIterations < MaxIterations; ++NumIterations)
        {
            v3 Pos = RayPos + T * RayDir;
            f32 Dist = SmokeSdf(Pos);

            if (Dist < Epsilon)
            {
                Intersected = true;
                break;
            }

            T += Dist;
            if (T > MaxT)
            {
                break;
            }
        }
    }

    if (Intersected)
    {
        // NOTE: Light up our volume
        v3 View = RayDir;
        f32 Epsilon = 0.01f;
        
        v3 SurfaceColor = V3(1);
        v3 SurfacePos = RayPos + RayDir * T;
        v3 SurfaceNormal = Normalize(V3(SmokeSdf(SurfacePos + V3(Epsilon, 0, 0)) - SmokeSdf(SurfacePos - V3(Epsilon, 0, 0)),
                                        SmokeSdf(SurfacePos + V3(0, Epsilon, 0)) - SmokeSdf(SurfacePos - V3(0, Epsilon, 0)),
                                        SmokeSdf(SurfacePos + V3(0, 0, Epsilon)) - SmokeSdf(SurfacePos - V3(0, 0, Epsilon))));
        f32 SpecularExponent = 10.0f;

        // NOTE: Fog shading
        v3 MarchStart = SurfacePos;
        v3 MarchDir = RayDir;
        f32 MarchLength = RayLength - T;
        u32 NumSteps = 128;
        f32 StepLength = MarchLength / f32(NumSteps);
        v3 StepVec = MarchDir * StepLength;

        v3 CurrPos = MarchStart;
        v3 AccumFogColor = V3(0.0f);

        // NOTE: Attempt at realistic fog lighting
#if 0
        for (f32 CurrRayLength = 0; CurrRayLength < MarchLength - StepLength; CurrRayLength += StepLength)
        {
            // NOTE: Only step if we are in the volume
            if (SmokeSdf(CurrPos) < 0.0f)
            {
                v3 CurrFogColor = V3(0);

                // NOTE: Contribute point lights
                for (u32 PointLightId = 0; PointLightId < SceneBuffer.NumPointLights; ++PointLightId)
                {
                    point_light CurrLight = PointLights[PointLightId];
                    v3 LightColor = PointLightAttenuate(CurrPos, CurrLight);
                    f32 LDotV = Dot(-RayDir, Normalize(CurrPos - CurrLight.Pos));
                    f32 Phase = HenryGreensteinPhaseFunction(LDotV);
                    CurrFogColor += LightColor * Phase;
                }                

                // NOTE: Contribute directional light
                {
                    f32 LDotV = Dot(-RayDir, DirectionalLight.Dir);
                    f32 Phase = HenryGreensteinPhaseFunction(LDotV);
                    CurrFogColor += DirectionalLight.Color * Phase;
                }
                
                f32 DistanceAttenuation = Exp(-VolFogBuffer.Density * CurrRayLength);
                CurrFogColor *= DistanceAttenuation * StepLength;
                AccumFogColor += CurrFogColor;
            }
            
            CurrPos += StepVec;
        }

        v3 StartFogColor = BackgroundColor * Exp(-MarchLength * VolFogBuffer.Density);
        v3 FogColor = VolFogBuffer.Density * VolFogBuffer.Albedo * AccumFogColor + StartFogColor;

        // NOTE: Gamma correct our color
        OutColor = V4(Pow(FogColor, V3(1.0f / 2.2f)), 1);
#endif

        // NOTE: Tutorial fog lighting
#if 0
        v3 VolumetricColor = V3(0);
        f32 OpaqueVisibility = 1.0f;
        for (u32 StepId = 0; StepId < NumSteps; ++StepId)
        {
            f32 DistanceTravelled = StepLength * f32(StepId);
            v3 CurrPos = MarchStart + MarchDir * DistanceTravelled;
            if (SmokeSdf(CurrPos) < 0.0f)
            {
                f32 PreviousOpaqueVisibility = OpaqueVisibility;
                OpaqueVisibility *= Exp(-ABSORPTION_COEFF * DistanceTravelled);
                f32 AbsorptionFromMarch = PreviousOpaqueVisibility - OpaqueVisibility;

                for (u32 PointLightId = 0; PointLightId < SceneBuffer.NumPointLights; ++PointLightId)
                {
                    point_light CurrLight = PointLights[PointLightId];
                    v3 LightDir = CurrLight.Pos - CurrPos;
                    f32 LightDistance = Length(LightDir);
                    LightDir /= LightDistance;
                    
                    v3 LightColor = PointLightAttenuate(CurrPos, CurrLight) * CurrLight.Color;
                    f32 LightVisibility = 0.0f;//GetLightVisibility(CurrPos, LightDir, LightDistance, 4, LightDistance / 4.0f);
                    VolumetricColor += AbsorptionFromMarch * LightVisibility * VOLUME_ALBEDO * LightColor;
                }

                VolumetricColor += AbsorptionFromMarch * VOLUME_ALBEDO * DirectionalLight.AmbientLight;
            }
        }

        v3 BlendedColor = VolumetricColor + BackgroundColor * OpaqueVisibility;
        OutColor = V4(Pow(BlendedColor, V3(1.0f / 2.2f)), 1);

#endif
        
        // NOTE: Regular surface shading
#if 1
        v3 Color = V3(0);

        // NOTE: Calculate lighting for point lights
        for (u32 PointLightId = 0; PointLightId < SceneBuffer.NumPointLights; ++PointLightId)
        {
            point_light CurrLight = PointLights[PointLightId];
            Color += BlinnPhongPointLight(View, SurfacePos, SurfaceColor, SurfaceNormal, SpecularExponent, CurrLight);
        }
    
        // NOTE: Calculate lighting for directional lights
        {
            Color += BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, SpecularExponent, -DirectionalLight.Dir, DirectionalLight.Color);
            Color += DirectionalLight.AmbientLight;
        }
        
        OutColor = V4(Color, 1);
#endif
    }
    else
    {
        // NOTE: Gamma correct our color
        OutColor = V4(Pow(BackgroundColor, V3(1.0f / 2.2f)), 1);
    }
}

#endif
