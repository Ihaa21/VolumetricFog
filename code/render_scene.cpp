
//
// NOTE: Directional Light Functions
//

inline void DirectionalLightResize(render_scene* Scene, u32 Width, u32 Height, f32 DepthBiasConstant, f32 DepthBiasSlope, f32 DepthBiasClamp)
{
    directional_light_data* DirLight = &Scene->DirectionalLightData;
    
    b32 ReCreate = DirLight->Arena.Used != 0;
    VkArenaClear(&DirLight->Arena);

    DirLight->Width = Width;
    DirLight->Height = Height;
    DirLight->DepthBiasConstant = DepthBiasConstant;
    DirLight->DepthBiasSlope = DepthBiasSlope;
    DirLight->DepthBiasClamp = DepthBiasClamp;
    
    RenderTargetEntryReCreate(&DirLight->Arena, Width, Height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT, &DirLight->ShadowImage, &DirLight->ShadowEntry);

    if (ReCreate)
    {
        RenderTargetUpdateEntries(&DemoState->TempArena, &DirLight->RenderTarget);
    }

    VkDescriptorImageWrite(&RenderState->DescriptorManager, DirLight->Descriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           DirLight->ShadowEntry.View, DirLight->Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
}

inline void SceneDirectionalShadowLightSet(render_scene* Scene, v3 LightDir, f32 Intensity, v3 Color, v3 AmbientColor, v3 Pos, v3 BoundsMin,
                                           v3 BoundsMax)
{
    directional_light_data* DirLight = &Scene->DirectionalLightData;
    
    // TODO: Use infinite z?
    // NOTE: Lighting is done in camera space
    DirLight->Enabled = true;
    DirLight->GpuData.Dir = LightDir;
    DirLight->GpuData.Color = Intensity * Color;
    DirLight->GpuData.AmbientColor = AmbientColor;

    v3 Up = V3(0, 0, 1);
    f32 DotValue = Abs(Dot(Up, LightDir));
    if (DotValue > 0.99f && DotValue < 1.01f)
    {
        Up = V3(0, 1, 0);
    }

    DirLight->GpuData.VPTransform = (VkOrthoProjM4(BoundsMin.x, BoundsMax.x, BoundsMax.y, BoundsMin.y, BoundsMin.z, BoundsMax.z) *
                                     LookAtM4(LightDir, Up, Pos));
}

inline void SceneDirectionalLightSet(render_scene* Scene, v3 LightDir, f32 Intensity, v3 Color, v3 AmbientColor)
{
    // NOTE: Lighting is done in camera space
    directional_light_data* DirLight = &Scene->DirectionalLightData;
    DirLight->Enabled = false;
    DirLight->GpuData.Dir = LightDir;
    DirLight->GpuData.Color = Intensity * Color;
    DirLight->GpuData.AmbientColor = AmbientColor;
}

//
// NOTE: Render Scene Functions
//

inline u32 SceneMaterialAdd(render_scene* Scene, v3 DiffuseColor, v3 SpecularColor, float SpecularPower)
{
    Assert(Scene->NumMaterials < Scene->MaxNumMaterials);
    u32 MaterialId = Scene->NumMaterials++;

    gpu_blinn_phong_material* Material = Scene->Materials + MaterialId;
    Material->DiffuseColor = DiffuseColor;
    Material->SpecularColor = SpecularColor;
    Material->SpecularPower = SpecularPower;
    
    return MaterialId;
}

inline u32 SceneMaterialAdd(render_scene* Scene, asset_material Material)
{
    u32 Result = SceneMaterialAdd(Scene, Material.DiffuseColor, Material.SpecularColor, Material.SpecularPower);
    return Result;
}

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, VkBuffer VertexBuffer, VkBuffer IndexBuffer, u32 NumIndices,
                        u32 MaterialId = 0)
{
    Assert(Scene->NumRenderHandles < Scene->MaxNumRenderHandles);
    
    u32 HandleId = Scene->NumRenderHandles++;
    render_handle* Handle = Scene->RenderHandles + HandleId;
    Handle->Type = RenderHandleType_Mesh;

    render_mesh* Mesh = &Handle->Mesh;
    Mesh->Color = Color;
    Mesh->Normal = Normal;
    Mesh->VertexBuffer = VertexBuffer;
    Mesh->IndexBuffer = IndexBuffer;
    Mesh->NumIndices = NumIndices;
    Mesh->MaterialDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Scene->MaterialDescLayout);
    VkDescriptorImageWrite(&RenderState->DescriptorManager, Mesh->MaterialDescriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Color.View, DemoState->AnisoSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkDescriptorImageWrite(&RenderState->DescriptorManager, Mesh->MaterialDescriptor, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Normal.View, DemoState->AnisoSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, Mesh->MaterialDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->MaterialBuffer,
                            sizeof(gpu_blinn_phong_material) * MaterialId);

    return HandleId;
}

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, procedural_mesh Mesh, u32 MaterialId = 0)
{
    u32 Result = SceneMeshAdd(Scene, Color, Normal, Mesh.Vertices, Mesh.Indices, Mesh.NumIndices, MaterialId);
    return Result;
}

inline u32 SceneModelAdd(vk_commands* Commands, render_scene* Scene, linear_arena* Arena, asset_model Model)
{
    Assert(Scene->NumRenderHandles < Scene->MaxNumRenderHandles);
    
    u32 HandleId = Scene->NumRenderHandles++;
    render_handle* Handle = Scene->RenderHandles + HandleId;
    Handle->Type = RenderHandleType_Model;

    render_model* RenderModel = &Handle->Model;
    RenderModel->AssetModel = Model;
    RenderModel->MaterialDescriptors = PushArray(Arena, VkDescriptorSet, Model.NumMaterials);

    for (u32 MaterialId = 0; MaterialId < Model.NumMaterials; ++MaterialId)
    {
        asset_material* CurrMaterial = Model.MaterialArray + MaterialId;
        VkDescriptorSet* CurrDescriptor = RenderModel->MaterialDescriptors + MaterialId;
        *CurrDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Scene->MaterialDescLayout);

        u32 SceneMaterialId = SceneMaterialAdd(Scene, *CurrMaterial);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, *CurrDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->MaterialBuffer,
                                sizeof(gpu_blinn_phong_material) * SceneMaterialId);
        
        if (CurrMaterial->DiffuseTextureId != 0xFFFFFFFF)
        {
            vk_image DiffuseImage = Model.TextureArray[CurrMaterial->DiffuseTextureId];
        
            VkDescriptorImageWrite(&RenderState->DescriptorManager, *CurrDescriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   DiffuseImage.View, DemoState->AnisoSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            // TODO: Add correct normal maps
            VkDescriptorImageWrite(&RenderState->DescriptorManager, *CurrDescriptor, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   DiffuseImage.View, DemoState->AnisoSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        else
        {
            VkDescriptorImageWrite(&RenderState->DescriptorManager, *CurrDescriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   Scene->WhiteTexture.View, DemoState->AnisoSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            // TODO: Add correct normal maps
            VkDescriptorImageWrite(&RenderState->DescriptorManager, *CurrDescriptor, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   Scene->WhiteTexture.View, DemoState->AnisoSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    return HandleId;
}

inline void SceneOpaqueInstanceAdd(render_scene* Scene, u32 HandleId, m4 WTransform)
{
    Assert(Scene->NumOpaqueInstances < Scene->MaxNumOpaqueInstances);

    instance_entry* Instance = Scene->OpaqueInstances + Scene->NumOpaqueInstances++;
    Instance->HandleId = HandleId;
    Instance->ShadowWVP = Scene->DirectionalLightData.GpuData.VPTransform * WTransform;
    Instance->WTransform = WTransform;
    Instance->WVPTransform = CameraGetVP(&Scene->Camera)*Instance->WTransform;
}

inline void ScenePointLightAdd(render_scene* Scene, v3 Pos, v3 Color, f32 Radius)
{
    Assert(Scene->NumPointLights < Scene->MaxNumPointLights);

    // TODO: Specify strength or a sphere so that we can visualize nicely too?
    point_light* PointLight = Scene->PointLights + Scene->NumPointLights++;
    PointLight->Pos = Pos;
    PointLight->Color = Color;
    PointLight->Radius = Radius;
}

inline void RenderSceneCreate(linear_arena* Arena, linear_arena* TempArena, render_scene* Scene)
{
    *Scene = {};
    
    Scene->SceneBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                        sizeof(scene_globals));

    Scene->MaxNumRenderHandles = 1000;
    Scene->RenderHandles = PushArray(Arena, render_handle, Scene->MaxNumRenderHandles);

    Scene->MaxNumMaterials = 1000;
    Scene->Materials = PushArray(Arena, gpu_blinn_phong_material, Scene->MaxNumMaterials);
    Scene->MaterialBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           sizeof(gpu_blinn_phong_material)*Scene->MaxNumMaterials);

    Scene->MaxNumOpaqueInstances = 1000;
    Scene->OpaqueInstances = PushArray(Arena, instance_entry, Scene->MaxNumOpaqueInstances);
    Scene->OpaqueInstanceBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 sizeof(gpu_instance_entry)*Scene->MaxNumOpaqueInstances);
        
    Scene->MaxNumPointLights = 1000;
    Scene->PointLights = PushArray(Arena, point_light, Scene->MaxNumPointLights);
    Scene->PointLightBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             sizeof(point_light)*Scene->MaxNumPointLights);
    Scene->PointLightTransforms = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 sizeof(m4)*Scene->MaxNumPointLights);

    Scene->DirectionalLightData.RenderGlobals = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                               sizeof(gpu_directional_light));
    Scene->DirectionalLightData.InstanceTransforms = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                    sizeof(m4)*Scene->MaxNumOpaqueInstances);
        
    // NOTE: Create general descriptor set layouts
    {
        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Scene->MaterialDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }

        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Scene->SceneDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }
    }

    // NOTE: Populate descriptors
    Scene->SceneDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Scene->SceneDescLayout);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Scene->SceneBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->OpaqueInstanceBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->PointLightBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->PointLightTransforms);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->DirectionalLightData.RenderGlobals);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->DirectionalLightData.InstanceTransforms);

    // NOTE: Add default material
    SceneMaterialAdd(Scene, V3(1), V3(0), 1);

    // NOTE: Shadow Desc Layout
    {
        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Scene->ShadowDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }
        
        directional_light_data* DirLight = &Scene->DirectionalLightData;
            
        u64 HeapSize = GigaBytes(1);
        DirLight->Arena = VkLinearArenaCreate(RenderState->Device, RenderState->LocalMemoryId, HeapSize);

        DirLight->Sampler = VkSamplerCreate(RenderState->Device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0);
        DirLight->Descriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Scene->ShadowDescLayout);
        DirectionalLightResize(Scene, 1, 1, 0, 0, 0);

        // NOTE: Shadow RT
        {
            render_target_builder Builder = RenderTargetBuilderBegin(Arena, TempArena, 1, 1);
            RenderTargetAddTarget(&Builder, &DirLight->ShadowEntry, VkClearDepthStencilCreate(0, 0));
                            
            vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(TempArena);

            u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, DirLight->ShadowEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
            VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            VkRenderPassSubPassEnd(&RpBuilder);

            VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);
                
            DirLight->RenderTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
        }
    
        // NOTE: Shadow PSO
        {
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_shadow_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v3));
            VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v2));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
            VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER);

            VkDynamicState DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
            VkPipelineSetDynamicStates(&Builder, DynamicStates, ArrayCount(DynamicStates));

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    Scene->MaterialDescLayout,
                    Scene->SceneDescLayout,
                };
            
            Scene->ShadowPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                         DirLight->RenderTarget.RenderPass, 0, DescriptorLayouts, ArrayCount(DescriptorLayouts));
        }
    }
}

inline void RenderSceneUpload(vk_commands* Commands, render_scene* Scene, f32 FrameTime)
{
    // NOTE: Upload instances
    if (Scene->NumOpaqueInstances > 0)
    {                    
        gpu_instance_entry* GpuData = VkCommandsPushWriteArray(Commands, Scene->OpaqueInstanceBuffer, gpu_instance_entry, Scene->NumOpaqueInstances,
                                                               BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                               BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

        for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
        {
            GpuData[InstanceId].WTransform = Scene->OpaqueInstances[InstanceId].WTransform;
            GpuData[InstanceId].WVPTransform = Scene->OpaqueInstances[InstanceId].WVPTransform;
        }
    }

    // NOTE: Push Point Lights
    if (Scene->NumPointLights > 0)
    {
        point_light* PointLights = VkCommandsPushWriteArray(Commands, Scene->PointLightBuffer, point_light, Scene->MaxNumPointLights,
                                                            BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                            BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
        m4* Transforms = VkCommandsPushWriteArray(Commands, Scene->PointLightTransforms, m4, Scene->NumPointLights,
                                                  BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                  BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

        for (u32 LightId = 0; LightId < Scene->NumPointLights; ++LightId)
        {
            point_light* CurrLight = Scene->PointLights + LightId;
            PointLights[LightId] = *CurrLight;
            // NOTE: Convert to view space
            v4 Test = CameraGetV(&Scene->Camera) * V4(CurrLight->Pos, 1.0f);
            Transforms[LightId] = CameraGetVP(&Scene->Camera) * M4Pos(CurrLight->Pos) * M4Scale(V3(CurrLight->Radius));
        }
    }

    // NOTE: Push Directional Lights
    {
        directional_light_data* DirLight = &Scene->DirectionalLightData;
        
        {
            gpu_directional_light* GpuData = VkCommandsPushWriteStruct(Commands, DirLight->RenderGlobals, gpu_directional_light,
                                                                       BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                                       BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            CopyStruct(&Scene->DirectionalLightData.GpuData, GpuData, gpu_directional_light);
        }
            
        // NOTE: Copy shadow data
        if (DirLight->Enabled && Scene->NumOpaqueInstances > 0)
        {
            m4* GpuData = VkCommandsPushWriteArray(Commands, DirLight->InstanceTransforms, m4, Scene->NumOpaqueInstances,
                                                   BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                   BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
            {
                GpuData[InstanceId] = Scene->OpaqueInstances[InstanceId].ShadowWVP;
            }
        }
    }

    // NOTE: Push Scene Globals
    {
        scene_globals* Data = VkCommandsPushWriteStruct(Commands, Scene->SceneBuffer, scene_globals,
                                                        BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                        BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
        *Data = {};
        Data->InvVpTransform = Inverse(CameraGetVP(&Scene->Camera));
        Data->CameraPos = Scene->Camera.Pos;
        Data->NumPointLights = Scene->NumPointLights;
        Data->RenderDim = V2(RenderState->WindowWidth, RenderState->WindowHeight);
        Data->CurrTime = Scene->TotalFrameTime;
        Data->CurrFrameId = Scene->FrameId++;

        Scene->TotalFrameTime += FrameTime;
    }
}

inline void RenderSceneRender(vk_commands* Commands, render_scene* Scene, vk_pipeline* Pipeline, b32 BindMaterialDescriptors)
{
    for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
    {
        instance_entry* CurrInstance = Scene->OpaqueInstances + InstanceId;
        render_handle* CurrHandle = Scene->RenderHandles + CurrInstance->HandleId;

        switch (CurrHandle->Type)
        {
            case RenderHandleType_Mesh:
            {
                render_mesh* Mesh = &CurrHandle->Mesh;

                if (BindMaterialDescriptors)
                {
                    VkDescriptorSet DescriptorSets[] =
                        {
                            Mesh->MaterialDescriptor,
                        };
                    vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Layout, 0,
                                            ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
                }
                
                VkDeviceSize Offset = 0;
                vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &Mesh->VertexBuffer, &Offset);
                vkCmdBindIndexBuffer(Commands->Buffer, Mesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(Commands->Buffer, Mesh->NumIndices, 1, 0, 0, InstanceId);
            } break;

            case RenderHandleType_Model:
            {
                render_model* Model = &CurrHandle->Model;
                for (u32 MeshId = 0; MeshId < Model->AssetModel.NumMeshes; ++MeshId)
                {
                    asset_mesh* Mesh = Model->AssetModel.MeshArray + MeshId;

                    if (BindMaterialDescriptors)
                    {
                        VkDescriptorSet DescriptorSets[] =
                            {
                                Model->MaterialDescriptors[Mesh->MaterialId],
                            };
                        vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Layout, 0,
                                                ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
                    }
                
                    VkDeviceSize VertexOffset = Mesh->VertexOffset;
                    VkDeviceSize IndexOffset = Mesh->IndexOffset;
                    vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &Model->AssetModel.VertexBuffer, &VertexOffset);
                    vkCmdBindIndexBuffer(Commands->Buffer, Model->AssetModel.IndexBuffer, IndexOffset, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(Commands->Buffer, Mesh->NumIndices, 1, 0, 0, InstanceId);
                }
            } break;
        }
    }
}
