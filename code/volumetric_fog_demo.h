#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

/*

  NOTE: The goal for this demo is to try various volumetric fog techniques. 

  References:

    - https://www.alexandre-pestana.com/volumetric-lights/
    
 */

#include "shadow_techniques.h"
#include "render_scene.h"

struct demo_state
{
    platform_block_arena PlatformBlockArena;
    linear_arena Arena;
    linear_arena TempArena;

    // NOTE: Render targets
    vk_linear_arena RenderTargetArena;
    VkImage ColorImage;
    render_target_entry ColorEntry;
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

    // NOTE: Saved model ids
    u32 Quad;
    u32 Cube;
    u32 Sphere;
    u32 Sponza;

    ui_state UiState;

    // NOTE: Shadow values
    u32 ShadowResX;
    u32 ShadowResY;
    f32 ShadowWorldDim;
    f32 ShadowWorldZDim;
    f32 DepthBiasConstant;
    f32 DepthBiasSlope;
    f32 DepthBiasClamp;
    v3 ShadowView;
    
    vk_pipeline* ForwardPipeline;

    // NOTE: Volumetric Fog Data
    VkDescriptorSetLayout VolFogDescLayout;
    VkDescriptorSet VolFogDescriptor;
    vk_pipeline* VolFogPipeline;
};

global demo_state* DemoState;
