#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#define MATH_GLSL 1
#include "../libs/math/math.h"

#include "shader_forward.h"

//=========================================================================================================================================
// NOTE: Shadow Vert
//=========================================================================================================================================

#if SHADOW_VERTEX

layout(location = 0) in v3 InPos;

layout(location = 0) out f32 OutDepth;

void main()
{
    v4 Position = DirectionalTransforms[gl_InstanceIndex] * V4(InPos, 1);
    gl_Position = Position;
    OutDepth = Position.z;
}

#endif

//=========================================================================================================================================
// NOTE: Forward Vert
//=========================================================================================================================================

#if FORWARD_VERTEX

layout(location = 0) in v3 InPos;
layout(location = 1) in v3 InNormal;
layout(location = 2) in v2 InUv;

layout(location = 0) out v3 OutWorldPos;
layout(location = 1) out v3 OutWorldNormal;
layout(location = 2) out v2 OutUv;
layout(location = 3) out v3 OutDirLightPos;

void main()
{
    instance_entry Entry = InstanceBuffer[gl_InstanceIndex];
    
    gl_Position = Entry.WVPTransform * V4(InPos, 1);
    OutWorldPos = (Entry.WTransform * V4(InPos, 1)).xyz;
    OutWorldNormal = (Entry.WTransform * V4(InNormal, 0)).xyz;
    OutUv = InUv;
    OutDirLightPos = (DirectionalTransforms[gl_InstanceIndex] * V4(InPos, 1)).xyz;
}

#endif

//=========================================================================================================================================
// NOTE: Forward Frag
//=========================================================================================================================================

#if FORWARD_FRAGMENT

layout(location = 0) in v3 InWorldPos;
layout(location = 1) in v3 InWorldNormal;
layout(location = 2) in v2 InUv;
layout(location = 3) in v3 InDirLightPos;

layout(location = 0) out v4 OutColor;

f32 DirLightOcclusionStandardGet(v3 SurfaceNormal, v3 LightDir, v3 LightPos)
{
    // NOTE: You can embedd the NDC transform in the matrix but then you need a separate set of transforms for each object
    v2 Uv = 0.5*LightPos.xy + V2(0.5);
    f32 Depth = texture(StandardShadowMap, Uv).x;

    // NOTE: We don't add a bias because its baked into the shadow map
    return step(Depth, LightPos.z);
}

void main()
{
    v3 CameraPos = SceneBuffer.CameraPos;

    v4 DiffuseTexelColor = texture(ColorTexture, InUv);

    if (DiffuseTexelColor.a == 0)
    {
        discard;
    }
    
    v3 SurfacePos = InWorldPos;
    v3 SurfaceNormal = Normalize(InWorldNormal);
    v3 SurfaceColor = DiffuseTexelColor.rgb;
    v3 View = Normalize(CameraPos - SurfacePos);
    v3 Color = V3(0);
    
    // NOTE: Calculate lighting for directional lights
    {
        f32 Occlusion = DirLightOcclusionStandardGet(SurfaceNormal, DirectionalLight.Dir, InDirLightPos);
        Occlusion = Occlusion == 0.0f ? 0.25f : Occlusion;
        
        Color += Occlusion*BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, 32, DirectionalLight.Dir, DirectionalLight.Color);
        Color += DirectionalLight.AmbientLight;
    }

    OutColor = V4(Color, 1);
}

#endif
