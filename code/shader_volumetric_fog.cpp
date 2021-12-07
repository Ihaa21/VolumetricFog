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
                                                                        
layout(set = 0, binding = 1) buffer directional_light_buffer 
{                                                                   
    directional_light_gpu DirectionalLight;                         
};                                                                  

layout(set = 0, binding = 2) uniform sampler2D Depth;
layout(set = 0, binding = 3) uniform sampler2D Shadow;

f32 ComputeScattering(f32 LDotV)
{
    // NOTE: Mie scaterring approximated with Henyey-Greenstein phase function.
    f32 GScattering = 0.8f;
    f32 GScatteringSq = GScattering * GScattering;
    f32 Result = 1.0f - GScatteringSq;
    Result /= (4.0f * Pi32 * pow(1.0f + GScatteringSq - (2.0f * GScattering) * LDotV, 0.5f));

    return Result;
}

//=========================================================================================================================================
// NOTE: Ray March Volumetric Fog Frag
//=========================================================================================================================================

#if RAYMARCH_FOG_FRAG

layout(location = 0) in v2 InUv;

layout(location = 0) out v4 OutColor;

void main()
{
    // NOTE: Reconstruct world pos from depth
    v3 WorldPos = V3(0);
    {
        f32 Depth = texture(Depth, InUv).x;
        v4 ProjectedPos = V4(2.0f * InUv - V2(1.0f), Depth, 1.0f);
        ProjectedPos = SceneBuffer.InvVpTransform * ProjectedPos;
        WorldPos = ProjectedPos.xyz / ProjectedPos.w;
    }

    // NOTE: We start from the camera and move into the world
    v3 CameraPos = SceneBuffer.CameraPos;
    v3 RayVector = WorldPos - CameraPos;
    f32 RayLength = Length(RayVector);
    v3 RayDir = RayVector / RayLength;

    u32 NumSteps = 128;
    f32 StepLength = RayLength / NumSteps;
    v3 Step = RayDir * StepLength;

    v3 CurrPos = CameraPos;
    f32 AccumFog = 0.0f;
    for (u32 StepId = 0; StepId < NumSteps; ++StepId)
    {
        v4 ShadowSpacePos = DirectionalLight.VPTransform * V4(CurrPos, 1);
        ShadowSpacePos.xyz /= ShadowSpacePos.w;
        v2 ShadowUv = 0.5f * (ShadowSpacePos.xy + V2(1));

        float ShadowValue = texture(Shadow, ShadowUv).x;
        if (ShadowValue < ShadowSpacePos.z)
        {
            // NOTE: Position is not in shadow, accumulate fog
            f32 LDotV = Dot(RayDir, DirectionalLight.Dir);
            AccumFog += ComputeScattering(LDotV);
        }
        
        CurrPos += Step;
    }

    AccumFog /= f32(NumSteps);
    v3 FogColor = 1.0f * V3(AccumFog) * V3(DirectionalLight.Color);

    OutColor = V4(FogColor, 1);
}

#endif
