
#include "volumetric_fog_demo.h"
#include "shadow_techniques.cpp"
#include "render_scene.cpp"

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
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                                  &DemoState->ColorImage, &DemoState->ColorEntry);
        RenderTargetEntryReCreate(&DemoState->RenderTargetArena, Width, Height, VK_FORMAT_D32_SFLOAT,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                                  VK_IMAGE_ASPECT_DEPTH_BIT, &DemoState->DepthImage, &DemoState->DepthEntry);

        if (ReCreate)
        {
            RenderTargetUpdateEntries(&DemoState->TempArena, &DemoState->ForwardRenderTarget);
        }

        VkDescriptorImageWrite(&RenderState->DescriptorManager, *OutputRtSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               DemoState->ColorEntry.View, DemoState->LinearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

    DemoState->ShadowResX = 2048;
    DemoState->ShadowResY = 2048;
    DemoState->ShadowWorldDim = 10.0f;
    DemoState->ShadowWorldZDim = 10.0f;
    DemoState->ShadowView = V3(0.3f, 0.0f, -1.0f);
    DemoState->DepthBiasSlope = 3.0f;
    
    // NOTE: Init scene system
    RenderSceneCreate(&DemoState->Arena, &DemoState->TempArena, &DemoState->Scene, DemoState->ShadowResX, DemoState->ShadowResY);
    DemoState->Scene.Camera = CameraFpsCreate(V3(0, -5, 2), V3(0, 1, 0), true, 1.0f, 1.0f);
    CameraSetPersp(&DemoState->Scene.Camera, f32(RenderState->WindowWidth / RenderState->WindowHeight), 90.0f, 0.01f, 1000.0f);

    // NOTE: Create render data
    {
        u32 RenderWidth = RenderState->WindowWidth;
        u32 RenderHeight = RenderState->WindowHeight;
        
        DemoState->RenderTargetArena = VkLinearArenaCreate(RenderState->Device, RenderState->LocalMemoryId, GigaBytes(1));
        RenderTargetSwapChainChange(RenderWidth, RenderHeight, RenderState->SwapChainFormat, &DemoState->Scene, &DemoState->CopyToSwapDesc);
    
        // NOTE: Forward RT
        {
            render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, RenderWidth, RenderHeight);
            RenderTargetAddTarget(&Builder, &DemoState->ColorEntry, VkClearColorCreate(0, 0, 0, 1));
            RenderTargetAddTarget(&Builder, &DemoState->DepthEntry, VkClearDepthStencilCreate(0, 0));
                            
            vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

            u32 ColorId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->ColorEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->DepthEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
            VkRenderPassColorRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            VkRenderPassSubPassEnd(&RpBuilder);

            // NOTE: Sync depth writes so we can read depth
            VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);
            
            VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
            VkRenderPassInputRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            VkRenderPassColorRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
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

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->Scene.MaterialDescLayout,
                    DemoState->Scene.SceneDescLayout,
                    DemoState->Scene.ShadowDescLayout,
                };
            
            DemoState->ForwardPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager, DemoState->ForwardRenderTarget.RenderPass, 0,
                                                              DescriptorLayouts, ArrayCount(DescriptorLayouts));
        }

        // NOTE: Create Volumetric Fog Data
        {
            render_scene* Scene = &DemoState->Scene;
            
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->VolFogDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }

            // NOTE: Vol Fog Pipeline
            {
                VkDescriptorSetLayout Layouts[] =
                    {
                        DemoState->VolFogDescLayout,
                    };

                vk_pipeline_builder Builder = FullScreenPipelineBuilderCreate("shader_raymarch_fog_frag.spv", "main");

                // NOTE: Set the blending state
                VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
                                             VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
                
                DemoState->VolFogPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                                 DemoState->ForwardRenderTarget.RenderPass, 1, Layouts, ArrayCount(Layouts));
            }

            DemoState->VolFogDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->VolFogDescLayout);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->VolFogDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Scene->SceneBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->VolFogDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->ShadowData.RenderGlobals);
            VkDescriptorImageWrite(&RenderState->DescriptorManager, DemoState->VolFogDescriptor, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   DemoState->DepthEntry.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            VkDescriptorImageWrite(&RenderState->DescriptorManager, DemoState->VolFogDescriptor, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   Scene->ShadowData.ShadowEntry.View, Scene->ShadowData.Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        
        VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
    }
    
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
            Scene->WhiteTexture = VkImageCreate(RenderState->Device, &RenderState->GpuArena, Dim, Dim, VK_FORMAT_R8G8B8A8_UNORM,
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
    
    RenderTargetSwapChainChange(RenderState->WindowWidth, RenderState->WindowHeight, RenderState->SwapChainFormat, &DemoState->Scene, &DemoState->CopyToSwapDesc);
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
        ui_panel Panel = UiPanelBegin(UiState, &PanelPos, "Shadow Panel");
        
        f32 FilterSize = 0;
        f32 BlurFilterSize = 0;
        
        {
            UiPanelText(&Panel, "Shadow Data:");

            local_global f32 ResolutionX = f32(DemoState->ShadowResX);
            local_global f32 ResolutionY = f32(DemoState->ShadowResY);
            
            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "Resolution X:");
            UiPanelHorizontalSlider(&Panel, 1.0f, 4096.0f, &ResolutionX);
            UiPanelNumberBox(&Panel, &ResolutionX);
            UiPanelNextRow(&Panel);

            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "Resolution Y:");
            UiPanelHorizontalSlider(&Panel, 1.0f, 4096.0f, &ResolutionY);
            UiPanelNumberBox(&Panel, &ResolutionY);
            UiPanelNextRow(&Panel);

            if (DemoState->ShadowResX != u32(ResolutionX) || DemoState->ShadowResY != u32(ResolutionY))
            {
                shadow_data* ShadowData = &DemoState->Scene.ShadowData;
                ShadowResize(&DemoState->Scene, u32(ResolutionX), u32(ResolutionY));
                VkDescriptorImageWrite(&RenderState->DescriptorManager, DemoState->VolFogDescriptor, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       ShadowData->ShadowEntry.View, ShadowData->Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            
            DemoState->ShadowResX = u32(ResolutionX);
            DemoState->ShadowResY = u32(ResolutionY);
            
            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "World Dim:");
            UiPanelHorizontalSlider(&Panel, 0.0f, 32.0f, &DemoState->ShadowWorldDim);
            UiPanelNumberBox(&Panel, &DemoState->ShadowWorldDim);
            UiPanelNextRow(&Panel);

            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "World Z Dim:");
            UiPanelHorizontalSlider(&Panel, 0.0f, 100.0f, &DemoState->ShadowWorldZDim);
            UiPanelNumberBox(&Panel, &DemoState->ShadowWorldZDim);
            UiPanelNextRow(&Panel);

            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "Depth Bias Constant:");
            UiPanelHorizontalSlider(&Panel, 0.0f, 3.0f, &DemoState->DepthBiasConstant);
            UiPanelNumberBox(&Panel, &DemoState->DepthBiasConstant);
            UiPanelNextRow(&Panel);

            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "Depth Bias Slope:");
            UiPanelHorizontalSlider(&Panel, 0.0f, 3.0f, &DemoState->DepthBiasSlope);
            UiPanelNumberBox(&Panel, &DemoState->DepthBiasSlope);
            UiPanelNextRow(&Panel);

            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "Depth Bias Clamp:");
            UiPanelHorizontalSlider(&Panel, 0.0f, 3.0f, &DemoState->DepthBiasClamp);
            UiPanelNumberBox(&Panel, &DemoState->DepthBiasClamp);
            UiPanelNextRow(&Panel);
            
            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "View X:");
            UiPanelHorizontalSlider(&Panel, -1.0f, 1.0f, &DemoState->ShadowView.x);
            UiPanelNumberBox(&Panel, &DemoState->ShadowView.x);
            UiPanelNextRow(&Panel);
            
            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "View Y:");
            UiPanelHorizontalSlider(&Panel, -1.0f, 1.0f, &DemoState->ShadowView.y);
            UiPanelNumberBox(&Panel, &DemoState->ShadowView.y);
            UiPanelNextRow(&Panel);
            
            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "View Z:");
            UiPanelHorizontalSlider(&Panel, -1.0f, 1.0f, &DemoState->ShadowView.z);
            UiPanelNumberBox(&Panel, &DemoState->ShadowView.z);
            UiPanelNextRow(&Panel);
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
        {
            local_global f32 T = 0.0f;
            T += 0.001f;
            if (T > 2.0f * Pi32)
            {
                T = 0.0f;
            }

            v3 LightDir = Normalize(DemoState->ShadowView);
            f32 Radius = 0.5f*DemoState->ShadowWorldDim;

            v3 MinPoint = -V3(Radius, Radius, 0.5f * DemoState->ShadowWorldZDim);
            v3 MaxPoint = V3(Radius, Radius, 0.5f * DemoState->ShadowWorldZDim);
            SceneDirectionalLightSet(Scene, LightDir, V3(1.0f, 1.0f, 1.0f), V3(0.15f), Scene->Camera.Pos, MinPoint, MaxPoint);
            
            // NOTE: Add Instances
            {
                // NOTE: Test scene
#if 0
                SceneOpaqueInstanceAdd(Scene, DemoState->Sphere, M4Pos(V3(0.0f, 0.0f, 2.0f)) * M4Scale(V3(1.0f)));
                
                SceneOpaqueInstanceAdd(Scene, DemoState->Cube, M4Pos(V3(0, 0, 0)) * M4Scale(V3(10, 10, 1)));
                SceneOpaqueInstanceAdd(Scene, DemoState->Cube, M4Pos(V3(0, -2, 0)) * M4Scale(V3(10, 1, 10)));
#endif

                // NOTE: Sponza
#if 1
                SceneOpaqueInstanceAdd(Scene, DemoState->Sponza, M4Rotation(V3(Pi32 / 2.0f, Pi32 / 2.0f, 0.0f)) * M4Scale(V3(5)));
#endif
            }
        }        

        RenderSceneUpload(Commands, &DemoState->Scene);
        
        VkCommandsTransferFlush(Commands, RenderState->Device);
    }

    // NOTE: Render Scene and Shadows
    {
        render_scene* Scene = &DemoState->Scene;
        shadow_data* ShadowData = &Scene->ShadowData;
    
        // NOTE: Generate Directional Shadow Map
        RenderTargetPassBegin(&ShadowData->RenderTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
        {
            vkCmdSetDepthBias(Commands->Buffer, DemoState->DepthBiasConstant, -DemoState->DepthBiasClamp, -DemoState->DepthBiasSlope);
            
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
                        ShadowData->Descriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->ForwardPipeline->Layout, 1,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }

            RenderSceneRender(Commands, &DemoState->Scene, DemoState->ForwardPipeline, true);
        }
        RenderTargetNextSubPass(Commands);
        // NOTE: Draw Volumetric Fog
        FullScreenPassRender(Commands, DemoState->VolFogPipeline, 1, &DemoState->VolFogDescriptor);
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
