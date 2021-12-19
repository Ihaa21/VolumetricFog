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

layout(set = 0, binding = 2) uniform vol_fog_buffer
{
    f32 GScattering; // NOTE: Anisotropy, probability of forward scattering
    f32 Density; // NOTE: Probability of being absorbed
    f32 Albedo; // NOTE: Probability of scattering (in/out)
} VolFogBuffer;

layout (input_attachment_index = 0, set = 0, binding = 3) uniform subpassInput Color;
layout (input_attachment_index = 1, set = 0, binding = 4) uniform subpassInput Depth;
layout(set = 0, binding = 5) uniform sampler2D Shadow;

f32 HenryGreensteinPhaseFunction(f32 LDotV)
{
    f32 GSquare = VolFogBuffer.GScattering * VolFogBuffer.GScattering;
    f32 Result = (1.0f - GSquare) / (4.0f * Pi32 * pow(1.0f + GSquare - 2.0f * VolFogBuffer.GScattering * LDotV, 1.5f));
    return Result;
}

v4 ProjectAndSampleTexture(sampler2D Texture, m4 Transform, v3 Pos)
{
    v4 TextureSpacePos = Transform * V4(Pos, 1);
    TextureSpacePos.xyz /= TextureSpacePos.w;
    v2 Uv = 0.5f * (TextureSpacePos.xy + V2(1));
    v4 Result = texture(Texture, Uv);

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
        f32 Depth = subpassLoad(Depth).x;
        v4 ProjectedPos = V4(2.0f * InUv - V2(1.0f), Depth, 1.0f);
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
    v3 L0 = subpassLoad(Color).rgb;
    v3 StartFog = L0 * Exp(-RayLength * VolFogBuffer.Density);
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

    // NOTE: Gamma correct our color
    FogColor = Pow(FogColor, V3(1.0f / 2.2f));
    
    OutColor = V4(FogColor, 1);
}

#endif
