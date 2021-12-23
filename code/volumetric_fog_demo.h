#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

/*

  NOTE: The goal for this demo is to try various volumetric fog techniques. 

  TODO:

   - add random ray offsets + blur (only need to figure out how to jitter)
   - once I get better at SDFs, go back to the smoke sim

  References:

    - https://www.alexandre-pestana.com/volumetric-lights/
    - https://wallisc.github.io/rendering/2020/05/02/Volumetric-Rendering-Part-1.html
    - https://wallisc.github.io/rendering/2020/05/02/Volumetric-Rendering-Part-2.html
    
 */

#include "shadow_techniques.h"
#include "render_scene.h"

enum scene_id
{
    Scene_None,
    Scene_Sponza,
    Scene_Smoke,
};

struct gpu_vol_fog_buffer
{
    v3 Albedo;
    f32 GScattering;
    v3 FogMinPos;
    f32 Density;
    v3 FogMaxPos;
    u32 Pad;
    v3 FogNumTexels;
    f32 CurrFrameTime;
};

struct sponza_scene
{
    b32 Render3dFog;
    
    // NOTE: Directional Light Ui values
    v3 DirLightView;
    f32 LightIntensity;
    f32 ShadowWorldDim;
    f32 ShadowWorldZDim;

    gpu_vol_fog_buffer VolFogBufferCpu;
    VkBuffer VolFogBuffer;
    vk_image Fog3dTexture;

    VkDescriptorSetLayout VolFogDescLayout;
    VkDescriptorSet VolFogDescriptor;

    VkDescriptorSetLayout GenerateFogDescLayout;
    VkDescriptorSet GenerateFogDescriptor;
    
    // NOTE: Constant Density Fog Data
    vk_pipeline* VolFogPipeline;

    // NOTE: 3d Texture Fog Data
    vk_pipeline* GenerateFogPipeline;
    vk_pipeline* VolFog3dPipeline;
};

struct smoke_scene
{
    VkDescriptorSetLayout VolSmokeDescLayout;
    VkDescriptorSet VolSmokeDescriptor;
    gpu_vol_fog_buffer VolFogBufferCpu;
    VkBuffer VolFogBuffer;
    vk_pipeline* VolSmokePipeline;
};

struct demo_state
{
    platform_block_arena PlatformBlockArena;
    linear_arena Arena;
    linear_arena TempArena;

    // NOTE: Render targets
    vk_linear_arena RenderTargetArena;
    VkImage ColorImage;
    render_target_entry ColorEntry;
    VkImage NormalImage;
    render_target_entry NormalEntry;
    VkImage FogAppliedImage;
    render_target_entry FogAppliedEntry;
    VkImage DepthImage;
    render_target_entry DepthEntry;
    render_target ForwardRenderTarget;
    
    // NOTE: Samplers
    VkSampler PointSampler;
    VkSampler LinearSampler;
    VkSampler AnisoSampler;
    
    // NOTE: Render Target Entries
    render_target_entry SwapChainEntry;
    render_target CopyToSwapTarget;
    VkDescriptorSet CopyToSwapDesc;
    vk_pipeline* CopyToSwapPipeline;

    render_scene Scene;
    u32 SceneId;
    sponza_scene SponzaScene;
    smoke_scene SmokeScene;

    // NOTE: Saved model ids
    u32 Quad;
    u32 Cube;
    u32 Sphere;
    u32 Sponza;

    ui_state UiState;
    
    vk_pipeline* ForwardPipeline;
};

global demo_state* DemoState;
