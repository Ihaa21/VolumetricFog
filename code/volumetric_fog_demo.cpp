
#include "volumetric_fog_demo.h"
#include "shadow_techniques.cpp"
#include "render_scene.cpp"

//
// NOTE: Sponza Scene
//

inline sponza_scene SponzaSceneInit(render_scene* Scene)
{
    sponza_scene Result = {};
    DemoState->SceneId = Scene_Sponza;
    
    Result.ShadowWorldDim = 10.0f;
    Result.ShadowWorldZDim = 10.0f;
    Result.LightIntensity = 2.5f;
    Result.DirLightView = V3(0.3f, 0.0f, -1.0f);
    Result.VolFogBufferCpu.Albedo = V3(0.62f, 0.56f, 0.39f);
    Result.VolFogBufferCpu.Density = 1.0f;
    Result.VolFogBufferCpu.GScattering = 0.4f;

    Result.VolFogBufferCpu.FogMinPos = V3(0.0f, 1.5f, -0.4f) - V3(3.0f, 5.0f, 2.0f);
    Result.VolFogBufferCpu.FogMaxPos = V3(0.0f, 1.5f, -0.4f) + V3(3.0f, 5.0f, 2.0f);
    Result.VolFogBufferCpu.FogNumTexels = V3(256);

    DirectionalLightResize(Scene, 4096, 4096, 0.0f, 3.0f, 0.0f);

    DemoState->Scene.Camera = CameraFpsCreate(V3(0, -5, 2), V3(0, 1, 0), true, 1.0f, 1.0f);
    CameraSetPersp(&DemoState->Scene.Camera, f32(RenderState->WindowWidth / RenderState->WindowHeight), 90.0f, 0.01f, 1000.0f);
    
    // NOTE: Create Volumetric Fog Data
    {
        // NOTE: Ray March Shader
        {
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Result.VolFogDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }

            VkDescriptorSetLayout Layouts[] =
                {
                    Result.VolFogDescLayout,
                    Scene->SceneDescLayout,
                };

            // NOTE: Constant Density Shader
            {
                vk_pipeline_builder Builder = FullScreenPipelineBuilderCreate("shader_raymarch_fog_frag.spv", "main");

                // NOTE: Set the blending state
                VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
                                             VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
                
                Result.VolFogPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                             DemoState->ForwardRenderTarget.RenderPass, 1, Layouts, ArrayCount(Layouts));
            }

            // NOTE: 3d Texture Shader
            {
                vk_pipeline_builder Builder = FullScreenPipelineBuilderCreate("shader_raymarch_fog_3d_frag.spv", "main");

                // NOTE: Set the blending state
                VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
                                             VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
                
                Result.VolFog3dPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                               DemoState->ForwardRenderTarget.RenderPass, 1, Layouts, ArrayCount(Layouts));
            }
        }

        // NOTE: Generate Fog Shader
        {
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Result.GenerateFogDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }

            VkDescriptorSetLayout Layouts[] =
                {
                    Result.GenerateFogDescLayout,
                };

            Result.GenerateFogPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                 "shader_generate_3d_fog.spv", "main", Layouts, ArrayCount(Layouts));
        }
        
        Result.VolFogBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             sizeof(gpu_vol_fog_buffer));
        Result.Fog3dTexture = VkImageCreate(RenderState->Device, &RenderState->GpuArena, u32(Result.VolFogBufferCpu.FogNumTexels.x),
                                            u32(Result.VolFogBufferCpu.FogNumTexels.y), u32(Result.VolFogBufferCpu.FogNumTexels.z),
                                            VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                            VK_IMAGE_ASPECT_COLOR_BIT);
            
        Result.GenerateFogDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Result.GenerateFogDescLayout);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Result.GenerateFogDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Result.VolFogBuffer);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, Result.GenerateFogDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                               Result.Fog3dTexture.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_GENERAL);
        
        Result.VolFogDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Result.VolFogDescLayout);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Result.VolFogDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Result.VolFogBuffer);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, Result.VolFogDescriptor, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                               DemoState->ColorEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, Result.VolFogDescriptor, 2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                               DemoState->NormalEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, Result.VolFogDescriptor, 3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                               DemoState->DepthEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, Result.VolFogDescriptor, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               Scene->DirectionalLightData.ShadowEntry.View, Scene->DirectionalLightData.Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, Result.VolFogDescriptor, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               Result.Fog3dTexture.View, DemoState->LinearSampler, VK_IMAGE_LAYOUT_GENERAL);
    }

    return Result;
}

inline void SponzaSceneUi(sponza_scene* SponzaScene, ui_panel* Panel)
{
    directional_light_data* DirLight = &DemoState->Scene.DirectionalLightData;
    
    local_global f32 ResolutionX = f32(DirLight->Width);
    local_global f32 ResolutionY = f32(DirLight->Height);
            
    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "Render 3d Fog:");
    UiPanelCheckBox(Panel, &SponzaScene->Render3dFog);
    UiPanelNextRow(Panel);
            
    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "Resolution X:");
    UiPanelHorizontalSlider(Panel, 1.0f, 4096.0f, &ResolutionX);
    UiPanelNumberBox(Panel, &ResolutionX);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "Resolution Y:");
    UiPanelHorizontalSlider(Panel, 1.0f, 4096.0f, &ResolutionY);
    UiPanelNumberBox(Panel, &ResolutionY);
    UiPanelNextRow(Panel);

    if (DirLight->Width != u32(ResolutionX) || DirLight->Height != u32(ResolutionY))
    {
        DirectionalLightResize(&DemoState->Scene, u32(ResolutionX), u32(ResolutionY), DirLight->DepthBiasConstant,
                               DirLight->DepthBiasSlope, DirLight->DepthBiasClamp);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, SponzaScene->VolFogDescriptor, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               DirLight->ShadowEntry.View, DirLight->Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "G Scattering:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SponzaScene->VolFogBufferCpu.GScattering);
    UiPanelNumberBox(Panel, &SponzaScene->VolFogBufferCpu.GScattering);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "Density:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SponzaScene->VolFogBufferCpu.Density);
    UiPanelNumberBox(Panel, &SponzaScene->VolFogBufferCpu.Density);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "AlbedoR:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SponzaScene->VolFogBufferCpu.Albedo.r);
    UiPanelNumberBox(Panel, &SponzaScene->VolFogBufferCpu.Albedo.r);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "AlbedoG:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SponzaScene->VolFogBufferCpu.Albedo.g);
    UiPanelNumberBox(Panel, &SponzaScene->VolFogBufferCpu.Albedo.g);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "AlbedoB:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SponzaScene->VolFogBufferCpu.Albedo.b);
    UiPanelNumberBox(Panel, &SponzaScene->VolFogBufferCpu.Albedo.b);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "Light Intensity:");
    UiPanelHorizontalSlider(Panel, 0.0f, 10.0f, &SponzaScene->LightIntensity);
    UiPanelNumberBox(Panel, &SponzaScene->LightIntensity);
    UiPanelNextRow(Panel);
            
    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "World Dim:");
    UiPanelHorizontalSlider(Panel, 0.0f, 32.0f, &SponzaScene->ShadowWorldDim);
    UiPanelNumberBox(Panel, &SponzaScene->ShadowWorldDim);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "World Z Dim:");
    UiPanelHorizontalSlider(Panel, 0.0f, 100.0f, &SponzaScene->ShadowWorldZDim);
    UiPanelNumberBox(Panel, &SponzaScene->ShadowWorldZDim);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "Depth Bias Constant:");
    UiPanelHorizontalSlider(Panel, 0.0f, 3.0f, &DirLight->DepthBiasConstant);
    UiPanelNumberBox(Panel, &DirLight->DepthBiasConstant);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "Depth Bias Slope:");
    UiPanelHorizontalSlider(Panel, 0.0f, 3.0f, &DirLight->DepthBiasSlope);
    UiPanelNumberBox(Panel, &DirLight->DepthBiasSlope);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "Depth Bias Clamp:");
    UiPanelHorizontalSlider(Panel, 0.0f, 3.0f, &DirLight->DepthBiasClamp);
    UiPanelNumberBox(Panel, &DirLight->DepthBiasClamp);
    UiPanelNextRow(Panel);
    
    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "View X:");
    UiPanelHorizontalSlider(Panel, -1.0f, 1.0f, &SponzaScene->DirLightView.x);
    UiPanelNumberBox(Panel, &SponzaScene->DirLightView.x);
    UiPanelNextRow(Panel);
            
    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "View Y:");
    UiPanelHorizontalSlider(Panel, -1.0f, 1.0f, &SponzaScene->DirLightView.y);
    UiPanelNumberBox(Panel, &SponzaScene->DirLightView.y);
    UiPanelNextRow(Panel);
            
    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "View Z:");
    UiPanelHorizontalSlider(Panel, -1.0f, 1.0f, &SponzaScene->DirLightView.z);
    UiPanelNumberBox(Panel, &SponzaScene->DirLightView.z);
    UiPanelNextRow(Panel);
}

inline void SponzaSceneUpdate(vk_commands* Commands, sponza_scene* SponzaScene, render_scene* Scene, f32 FrameTime)
{
    SponzaScene->VolFogBufferCpu.CurrFrameTime += FrameTime;
    
    v3 LightDir = Normalize(SponzaScene->DirLightView);
    f32 Radius = 0.5f*SponzaScene->ShadowWorldDim;

    v3 MinPoint = -V3(Radius, Radius, 0.5f * SponzaScene->ShadowWorldZDim);
    v3 MaxPoint = V3(Radius, Radius, 0.5f * SponzaScene->ShadowWorldZDim);
    SceneDirectionalShadowLightSet(Scene, LightDir, SponzaScene->LightIntensity, V3(1.0f, 1.0f, 1.0f), V3(0.05f), Scene->Camera.Pos,
                                   MinPoint, MaxPoint);

    {
        local_global f32 CurrT = 0.0f;
        if (CurrT > 2.0f * Pi32)
        {
            CurrT -= 2.0f * Pi32;
        }
        CurrT += FrameTime;
        v3 LightCenter = V3(-1.0f, 0.0f, -2.0f);
        ScenePointLightAdd(Scene, LightCenter + V3(Cos(CurrT), Sin(CurrT), 0.0f), V3(0.5f, 0.8f, 0.3f), 1.0f);
    }
    
    SceneOpaqueInstanceAdd(Scene, DemoState->Sponza, M4Rotation(V3(Pi32 / 2.0f, Pi32 / 2.0f, 0.0f)) * M4Scale(V3(5)));

    SponzaScene->VolFogBufferCpu.FogMinPos = V3(-1.25f, 0.3f, -0.4f) - V3(1.5f, 3.0f, 2.0f);
    SponzaScene->VolFogBufferCpu.FogMaxPos = V3(-1.25f, 0.3f, -0.4f) + V3(1.5f, 3.0f, 2.0f);
    
    // NOTE: Upload vol fog constants
    {
        gpu_vol_fog_buffer* GpuData = VkCommandsPushWriteStruct(Commands, SponzaScene->VolFogBuffer, gpu_vol_fog_buffer,
                                                                BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                                BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

        *GpuData = SponzaScene->VolFogBufferCpu;
    }
}

//
// NOTE: Smoke Scene
//

inline smoke_scene SmokeSceneInit(render_scene* Scene)
{
    smoke_scene Result = {};
    
    DemoState->SceneId = Scene_Smoke;

    Scene->Camera = CameraFpsCreate(V3(0, -5, 2), V3(0, 1, 0), true, 1.0f, 1.0f);
    CameraSetPersp(&Scene->Camera, f32(RenderState->WindowWidth / RenderState->WindowHeight), 90.0f, 0.01f, 1000.0f);
    
    // NOTE: Create Volumetric Smoke Data
    {
        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Result.VolSmokeDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }

        // NOTE: Vol Smoke Pipeline
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    Scene->SceneDescLayout,
                    Result.VolSmokeDescLayout,
                };

            vk_pipeline_builder Builder = FullScreenPipelineBuilderCreate("shader_raymarch_smoke_frag.spv", "main");

            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
                
            Result.VolSmokePipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                         DemoState->ForwardRenderTarget.RenderPass, 1, Layouts, ArrayCount(Layouts));
        }
            
        Result.VolFogBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             sizeof(gpu_vol_fog_buffer));

        Result.VolSmokeDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Result.VolSmokeDescLayout);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, Result.VolSmokeDescriptor, 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                               DemoState->ColorEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, Result.VolSmokeDescriptor, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                               DemoState->DepthEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Result.VolSmokeDescriptor, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Result.VolFogBuffer);
    }

    return Result;
}

inline void SmokeSceneUi(smoke_scene* SmokeScene, ui_panel* Panel)
{
    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "G Scattering:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SmokeScene->VolFogBufferCpu.GScattering);
    UiPanelNumberBox(Panel, &SmokeScene->VolFogBufferCpu.GScattering);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "Density:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SmokeScene->VolFogBufferCpu.Density);
    UiPanelNumberBox(Panel, &SmokeScene->VolFogBufferCpu.Density);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "AlbedoR:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SmokeScene->VolFogBufferCpu.Albedo.r);
    UiPanelNumberBox(Panel, &SmokeScene->VolFogBufferCpu.Albedo.r);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "AlbedoG:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SmokeScene->VolFogBufferCpu.Albedo.g);
    UiPanelNumberBox(Panel, &SmokeScene->VolFogBufferCpu.Albedo.g);
    UiPanelNextRow(Panel);

    UiPanelNextRowIndent(Panel);
    UiPanelText(Panel, "AlbedoB:");
    UiPanelHorizontalSlider(Panel, 0.0f, 1.0f, &SmokeScene->VolFogBufferCpu.Albedo.b);
    UiPanelNumberBox(Panel, &SmokeScene->VolFogBufferCpu.Albedo.b);
    UiPanelNextRow(Panel);
}

inline void SmokeSceneUpdate(vk_commands* Commands, smoke_scene* SmokeScene, render_scene* Scene, f32 FrameTime)
{
    SceneOpaqueInstanceAdd(Scene, DemoState->Quad, M4Scale(V3(100.0f, 100.0f, 1.0f)));

    v3 LightDir = Normalize(V3(0, 0, -1));
    SceneDirectionalLightSet(Scene, LightDir, 0.3f, V3(1.0f, 1.0f, 1.0f), V3(0.3f));

    // NOTE: Spawn 3 point lights
    v3 Colors[] =
    {
        V3(1, 0, 0),
        V3(0, 1, 0),
        V3(0, 0, 1),
    };
    
    local_global f32 CurrT = 0.0f;
    CurrT += FrameTime;
    if (CurrT > 2.0f * Pi32)
    {
        CurrT -= 2.0f * Pi32;
    }

    for (u32 LightId = 0; LightId < 3; ++LightId)
    {
        f32 AngleStart = 2.0f * Pi32 * f32(LightId) / 3.0f;
        ScenePointLightAdd(Scene, V3(Cos(AngleStart + CurrT), Sin(AngleStart + CurrT), 0.25f), 1.0f*Colors[LightId], 1.0f);
    }
    
    // NOTE: Upload vol fog constants
    {
        gpu_vol_fog_buffer* GpuData = VkCommandsPushWriteStruct(Commands, SmokeScene->VolFogBuffer, gpu_vol_fog_buffer,
                                                                BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                                BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

        *GpuData = SmokeScene->VolFogBufferCpu;
    }
}

//
// NOTE: Demo Code
//

inline void DemoAllocGlobals(linear_arena* Arena)
{
    // IMPORTANT: These are always the top of the program memory
    DemoState = PushStruct(Arena, demo_state);
    RenderState = PushStruct(Arena, render_state);
}

inline void RenderTargetSwapChainChange(u32 Width, u32 Height, VkFormat ColorFormat, render_scene* Scene, VkDescriptorSet* OutputRtSet)
{
    b32 ReCreate = DemoState->RenderTargetArena.Used != 0;
    VkArenaClear(&DemoState->RenderTargetArena);

    // NOTE: Render Target Data
    {
        RenderTargetEntryReCreate(&DemoState->RenderTargetArena, Width, Height, ColorFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, &DemoState->ColorImage, &DemoState->ColorEntry);
        RenderTargetEntryReCreate(&DemoState->RenderTargetArena, Width, Height, VK_FORMAT_R16G16B16A16_SFLOAT,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, &DemoState->NormalImage, &DemoState->NormalEntry);
        RenderTargetEntryReCreate(&DemoState->RenderTargetArena, Width, Height, ColorFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                                  &DemoState->FogAppliedImage, &DemoState->FogAppliedEntry);
        RenderTargetEntryReCreate(&DemoState->RenderTargetArena, Width, Height, VK_FORMAT_D32_SFLOAT,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                                  VK_IMAGE_ASPECT_DEPTH_BIT, &DemoState->DepthImage, &DemoState->DepthEntry);

        if (ReCreate)
        {
            RenderTargetUpdateEntries(&DemoState->TempArena, &DemoState->ForwardRenderTarget);
        }

        // NOTE: Update sponza descriptors
        sponza_scene* Sponza = &DemoState->SponzaScene;
        if (Sponza->VolFogDescriptor != VK_NULL_HANDLE)
        {
            VkDescriptorImageWrite(&RenderState->DescriptorManager, Sponza->VolFogDescriptor, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                                   DemoState->ColorEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VkDescriptorImageWrite(&RenderState->DescriptorManager, Sponza->VolFogDescriptor, 2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                                   DemoState->NormalEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VkDescriptorImageWrite(&RenderState->DescriptorManager, Sponza->VolFogDescriptor, 3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                                   DemoState->DepthEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }

        // NOTE: Update smoke descriptors
        smoke_scene* Smoke = &DemoState->SmokeScene;
        if (Smoke->VolSmokeDescriptor != VK_NULL_HANDLE)
        {
            VkDescriptorImageWrite(&RenderState->DescriptorManager, Smoke->VolSmokeDescriptor, 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                                   DemoState->ColorEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VkDescriptorImageWrite(&RenderState->DescriptorManager, Smoke->VolSmokeDescriptor, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                                   DemoState->DepthEntry.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }
        
        VkDescriptorImageWrite(&RenderState->DescriptorManager, *OutputRtSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               DemoState->FogAppliedEntry.View, DemoState->LinearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
        
    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
}

DEMO_INIT(Init)
{
    // NOTE: Init Memory
    {
        linear_arena Arena = LinearArenaCreate(ProgramMemory, ProgramMemorySize);
        DemoAllocGlobals(&Arena);
        *DemoState = {};
        *RenderState = {};
        DemoState->Arena = Arena;
        DemoState->TempArena = LinearSubArena(&DemoState->Arena, MegaBytes(10));
        DemoState->PlatformBlockArena = PlatformBlockArenaCreate(KiloBytes(64), 16);
    }

    // NOTE: Init Vulkan
    {
        {
            const char* DeviceExtensions[] =
            {
                "VK_EXT_shader_viewport_index_layer",
            };
            
            render_init_params InitParams = {};
            InitParams.ValidationEnabled = true;
            InitParams.WindowWidth = WindowWidth;
            InitParams.WindowHeight = WindowHeight;
            InitParams.GpuLocalSize = GigaBytes(1);
            InitParams.DeviceExtensionCount = ArrayCount(DeviceExtensions);
            InitParams.DeviceExtensions = DeviceExtensions;
            VkInit(VulkanLib, hInstance, WindowHandle, &DemoState->Arena, &DemoState->TempArena, &DemoState->PlatformBlockArena, InitParams);
        }
    }
    
    // NOTE: Create samplers
    DemoState->PointSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
    DemoState->LinearSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
    DemoState->AnisoSampler = VkSamplerMipMapCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, 16.0f,
                                                    VK_SAMPLER_MIPMAP_MODE_LINEAR, 0, 0, 5);    
        
    // NOTE: Init render target entries
    DemoState->SwapChainEntry = RenderTargetSwapChainEntryCreate(RenderState->WindowWidth, RenderState->WindowHeight,
                                                                 RenderState->SwapChainFormat);

    // NOTE: Copy To Swap RT
    {
        render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, RenderState->WindowWidth,
                                                                 RenderState->WindowHeight);
        RenderTargetAddTarget(&Builder, &DemoState->SwapChainEntry, VkClearColorCreate(0, 0, 0, 1));
                            
        vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);
        u32 ColorId = VkRenderPassAttachmentAdd(&RpBuilder, RenderState->SwapChainFormat, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
        VkRenderPassColorRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderPassSubPassEnd(&RpBuilder);

        DemoState->CopyToSwapTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
        DemoState->CopyToSwapPipeline = FullScreenCopyImageCreate(DemoState->CopyToSwapTarget.RenderPass, 0);
        DemoState->CopyToSwapDesc = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, RenderState->CopyImageDescLayout);
    }
    
    // NOTE: Init scene system
    RenderSceneCreate(&DemoState->Arena, &DemoState->TempArena, &DemoState->Scene);
    
    // NOTE: Create render data
    {
        u32 RenderWidth = RenderState->WindowWidth;
        u32 RenderHeight = RenderState->WindowHeight;
        
        DemoState->RenderTargetArena = VkLinearArenaCreate(RenderState->Device, RenderState->LocalMemoryId, GigaBytes(1));
        RenderTargetSwapChainChange(RenderWidth, RenderHeight, VK_FORMAT_R16G16B16A16_SFLOAT, &DemoState->Scene, &DemoState->CopyToSwapDesc);
    
        // NOTE: Forward RT
        {
            render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, RenderWidth, RenderHeight);
            RenderTargetAddTarget(&Builder, &DemoState->ColorEntry, VkClearColorCreate(1, 1, 1, 1));
            RenderTargetAddTarget(&Builder, &DemoState->NormalEntry, VkClearColorCreate(0, 0, 0, 0));
            RenderTargetAddTarget(&Builder, &DemoState->FogAppliedEntry, VkClearColorCreate(0, 0, 0, 1));
            RenderTargetAddTarget(&Builder, &DemoState->DepthEntry, VkClearDepthStencilCreate(0, 0));
                            
            vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

            u32 ColorId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->ColorEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            u32 NormalId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->NormalEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                     VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            u32 FogAppliedColorId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->FogAppliedEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                              VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->DepthEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
            VkRenderPassColorRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkRenderPassColorRefAdd(&RpBuilder, NormalId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            VkRenderPassSubPassEnd(&RpBuilder);

            // NOTE: Sync depth writes so we can read color/normal/depth
            VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);
            VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);
            
            VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
            VkRenderPassInputRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VkRenderPassInputRefAdd(&RpBuilder, NormalId, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VkRenderPassInputRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            VkRenderPassColorRefAdd(&RpBuilder, FogAppliedColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkRenderPassSubPassEnd(&RpBuilder);

            DemoState->ForwardRenderTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
        }

        // NOTE: Create Forward Pipeline
        {
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_forward_standard_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_forward_standard_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);

            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32_SFLOAT, sizeof(v2));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
            VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER);
                
            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->Scene.MaterialDescLayout,
                    DemoState->Scene.SceneDescLayout,
                    DemoState->Scene.ShadowDescLayout,
                };
            
            DemoState->ForwardPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager, DemoState->ForwardRenderTarget.RenderPass, 0,
                                                              DescriptorLayouts, ArrayCount(DescriptorLayouts));
        }
    }

    DemoState->SponzaScene = SponzaSceneInit(&DemoState->Scene);
    //DemoState->SmokeScene = SmokeSceneInit(&DemoState->Scene);
        
    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);

    // NOTE: Upload assets
    vk_commands* Commands = &RenderState->Commands;
    VkCommandsBegin(Commands, RenderState->Device);
    {
        render_scene* Scene = &DemoState->Scene;
        
        // NOTE: Push White Texture
        {
            u32 Texels[] =
                {
                    0xFFFFFFFF,
                };

            u32 Dim = 1;
            u32 ImageSize = Dim*Dim*sizeof(u32);
            Scene->WhiteTexture = VkImageCreate(RenderState->Device, &RenderState->GpuArena, Dim, Dim, VK_FORMAT_R8G8B8A8_SRGB,
                                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

            u8* GpuMemory = VkCommandsPushWriteImage(Commands, Scene->WhiteTexture.Image, Dim, Dim, ImageSize, VK_IMAGE_ASPECT_COLOR_BIT,
                                                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            Copy(Texels, GpuMemory, ImageSize);
        }
                        
        // NOTE: Push meshes
        DemoState->Quad = SceneMeshAdd(Scene, Scene->WhiteTexture, Scene->WhiteTexture, AssetsPushQuad());
        DemoState->Cube = SceneMeshAdd(Scene, Scene->WhiteTexture, Scene->WhiteTexture, AssetsPushCube());
        DemoState->Sphere = SceneMeshAdd(Scene, Scene->WhiteTexture, Scene->WhiteTexture, AssetsPushSphere(64, 64));
        DemoState->Sponza = SceneModelAdd(Commands, Scene, &DemoState->Arena, AssetsPushModel(Commands, &DemoState->Arena, &DemoState->TempArena, "sponza//", "sponza.obj"));

        UiStateCreate(RenderState->Device, &DemoState->Arena, &DemoState->TempArena, RenderState->LocalMemoryId,
                      &RenderState->DescriptorManager, &RenderState->PipelineManager, Commands, RenderState->SwapChainFormat,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &DemoState->UiState);

        // NOTE: Push materials
        {
            gpu_blinn_phong_material* GpuData = VkCommandsPushWriteArray(Commands, Scene->MaterialBuffer, gpu_blinn_phong_material, Scene->NumMaterials,
                                                                         BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                                         BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            CopyArray(Scene->Materials, GpuData, gpu_blinn_phong_material, Scene->NumMaterials);
        }
        
        VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
    }
    
    VkCommandsSubmit(Commands, RenderState->Device, RenderState->GraphicsQueue);
}

DEMO_DESTROY(Destroy)
{
}

DEMO_SWAPCHAIN_CHANGE(SwapChainChange)
{
    VkCheckResult(vkDeviceWaitIdle(RenderState->Device));
    VkSwapChainReCreate(&DemoState->TempArena, WindowWidth, WindowHeight, RenderState->PresentMode);

    DemoState->SwapChainEntry.Width = RenderState->WindowWidth;
    DemoState->SwapChainEntry.Height = RenderState->WindowHeight;

    DemoState->Scene.Camera.PerspAspectRatio = f32(RenderState->WindowWidth / RenderState->WindowHeight);
    
    RenderTargetSwapChainChange(RenderState->WindowWidth, RenderState->WindowHeight, VK_FORMAT_R16G16B16A16_SFLOAT, &DemoState->Scene, &DemoState->CopyToSwapDesc);
}

DEMO_CODE_RELOAD(CodeReload)
{
    linear_arena Arena = LinearArenaCreate(ProgramMemory, ProgramMemorySize);
    // IMPORTANT: We are relying on the memory being the same here since we have the same base ptr with the VirtualAlloc so we just need
    // to patch our global pointers here
    DemoAllocGlobals(&Arena);

    VkGetGlobalFunctionPointers(VulkanLib);
    VkGetInstanceFunctionPointers();
    VkGetDeviceFunctionPointers();
}

DEMO_MAIN_LOOP(MainLoop)
{
    u32 ImageIndex;
    VkCheckResult(vkAcquireNextImageKHR(RenderState->Device, RenderState->SwapChain, UINT64_MAX, RenderState->ImageAvailableSemaphore,
                                        VK_NULL_HANDLE, &ImageIndex));
    DemoState->SwapChainEntry.View = RenderState->SwapChainViews[ImageIndex];

    vk_commands* Commands = &RenderState->Commands;
    VkCommandsBegin(Commands, RenderState->Device);

    // NOTE: Update pipelines
    VkPipelineUpdateShaders(RenderState->Device, &RenderState->CpuArena, &RenderState->PipelineManager);

    RenderTargetUpdateEntries(&DemoState->TempArena, &DemoState->CopyToSwapTarget);

    // NOTE: Update Ui State
    {
        ui_state* UiState = &DemoState->UiState;
        
        ui_frame_input UiCurrInput = {};
        UiCurrInput.MouseDown = CurrInput->MouseDown;
        UiCurrInput.MousePixelPos = V2(CurrInput->MousePixelPos);
        UiCurrInput.MouseScroll = CurrInput->MouseScroll;
        CopyStruct(CurrInput->KeysDown, UiCurrInput.KeysDown, UiCurrInput.KeysDown);
        UiStateBegin(UiState, FrameTime, RenderState->WindowWidth, RenderState->WindowHeight, UiCurrInput);
        
        local_global v2 PanelPos = V2(100, 800);
        ui_panel Panel = UiPanelBegin(UiState, &PanelPos, "Fog Panel");
        UiPanelText(&Panel, "Fog Data:");

        switch (DemoState->SceneId)
        {
            case Scene_Sponza:
            {
                SponzaSceneUi(&DemoState->SponzaScene, &Panel);
            } break;

            case Scene_Smoke:
            {
                SmokeSceneUi(&DemoState->SmokeScene, &Panel);
            } break;
        }
        
        UiPanelEnd(&Panel);

        UiStateEnd(UiState, &RenderState->DescriptorManager);
    }
    
    // NOTE: Upload scene data
    {
        render_scene* Scene = &DemoState->Scene;
        Scene->NumOpaqueInstances = 0;
        Scene->NumPointLights = 0;
        if (!(DemoState->UiState.MouseTouchingUi || DemoState->UiState.ProcessedInteraction))
        {
            CameraUpdate(&Scene->Camera, CurrInput, PrevInput, FrameTime);
        }
        
        // NOTE: Populate scene
        switch (DemoState->SceneId)
        {
            case Scene_Sponza:
            {
                SponzaSceneUpdate(Commands, &DemoState->SponzaScene, Scene, FrameTime);
            } break;

            case Scene_Smoke:
            {
                SmokeSceneUpdate(Commands, &DemoState->SmokeScene, Scene, FrameTime);
            } break;
        }

        RenderSceneUpload(Commands, &DemoState->Scene, FrameTime);
        
        VkCommandsTransferFlush(Commands, RenderState->Device);
    }

    // NOTE: Generate our fog
    if (DemoState->SceneId == Scene_Sponza && DemoState->SponzaScene.Render3dFog)
    {
        sponza_scene* Sponza = &DemoState->SponzaScene;
        
        VkBarrierImageAdd(Commands, Sponza->Fog3dTexture.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                          0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
        VkCommandsBarrierFlush(Commands);

        u32 DispatchDim = CeilU32(Sponza->VolFogBufferCpu.FogNumTexels.x / 4.0f);
        VkComputeDispatch(Commands, Sponza->GenerateFogPipeline, &Sponza->GenerateFogDescriptor, 1, DispatchDim, DispatchDim, DispatchDim);

        VkBarrierImageAdd(Commands, Sponza->Fog3dTexture.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
        VkCommandsBarrierFlush(Commands);
    }
    
    // NOTE: Render Scene and Shadows
    {
        render_scene* Scene = &DemoState->Scene;
        directional_light_data* DirLight = &Scene->DirectionalLightData;
    
        // NOTE: Generate Directional Shadow Map
        RenderTargetPassBegin(&DirLight->RenderTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
        if (DirLight->Enabled)
        {
            vkCmdSetDepthBias(Commands->Buffer, DirLight->DepthBiasConstant, -DirLight->DepthBiasClamp, -DirLight->DepthBiasSlope);
            
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Scene->ShadowPipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        Scene->SceneDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Scene->ShadowPipeline->Layout, 1,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }

            RenderSceneRender(Commands, &DemoState->Scene, Scene->ShadowPipeline, false);
        }
        RenderTargetPassEnd(Commands);        
    
        // NOTE: Draw Meshes
        RenderTargetPassBegin(&DemoState->ForwardRenderTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
        {
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->ForwardPipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        Scene->SceneDescriptor,
                        DirLight->Descriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->ForwardPipeline->Layout, 1,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }

            RenderSceneRender(Commands, &DemoState->Scene, DemoState->ForwardPipeline, true);
        }
        RenderTargetNextSubPass(Commands);

        // NOTE: Draw Volumetric Fog
        switch (DemoState->SceneId)
        {
            case Scene_Sponza:
            {
                sponza_scene* Sponza = &DemoState->SponzaScene;
                
                VkDescriptorSet Descriptors[] =
                {
                    Sponza->VolFogDescriptor,
                    Scene->SceneDescriptor,
                };

                if (Sponza->Render3dFog)
                {
                    FullScreenPassRender(Commands, Sponza->VolFog3dPipeline, ArrayCount(Descriptors), Descriptors);
                }
                else
                {
                    FullScreenPassRender(Commands, Sponza->VolFogPipeline, ArrayCount(Descriptors), Descriptors);
                }
            } break;

            case Scene_Smoke:
            {
                VkDescriptorSet Descriptors[] =
                {
                    Scene->SceneDescriptor,
                    DemoState->SmokeScene.VolSmokeDescriptor,
                };
                
                FullScreenPassRender(Commands, DemoState->SmokeScene.VolSmokePipeline, ArrayCount(Descriptors), Descriptors);
            } break;
        }
        
        RenderTargetPassEnd(Commands);        
    }

    RenderTargetPassBegin(&DemoState->CopyToSwapTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
    FullScreenPassRender(Commands, DemoState->CopyToSwapPipeline, 1, &DemoState->CopyToSwapDesc);
    RenderTargetPassEnd(Commands);
    UiStateRender(&DemoState->UiState, RenderState->Device, Commands, DemoState->SwapChainEntry.View);

    VkCommandsEnd(Commands, RenderState->Device);
                    
    // NOTE: Render to our window surface
    // NOTE: Tell queue where we render to surface to wait
    VkPipelineStageFlags WaitDstMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo SubmitInfo = {};
    SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitInfo.waitSemaphoreCount = 1;
    SubmitInfo.pWaitSemaphores = &RenderState->ImageAvailableSemaphore;
    SubmitInfo.pWaitDstStageMask = &WaitDstMask;
    SubmitInfo.commandBufferCount = 1;
    SubmitInfo.pCommandBuffers = &Commands->Buffer;
    SubmitInfo.signalSemaphoreCount = 1;
    SubmitInfo.pSignalSemaphores = &RenderState->FinishedRenderingSemaphore;
    VkCheckResult(vkQueueSubmit(RenderState->GraphicsQueue, 1, &SubmitInfo, Commands->Fence));
    
    VkPresentInfoKHR PresentInfo = {};
    PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    PresentInfo.waitSemaphoreCount = 1;
    PresentInfo.pWaitSemaphores = &RenderState->FinishedRenderingSemaphore;
    PresentInfo.swapchainCount = 1;
    PresentInfo.pSwapchains = &RenderState->SwapChain;
    PresentInfo.pImageIndices = &ImageIndex;
    VkResult Result = vkQueuePresentKHR(RenderState->PresentQueue, &PresentInfo);

    switch (Result)
    {
        case VK_SUCCESS:
        {
        } break;

        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_SUBOPTIMAL_KHR:
        {
            // NOTE: Window size changed
            InvalidCodePath;
        } break;

        default:
        {
            InvalidCodePath;
        } break;
    }
}
