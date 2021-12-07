#pragma once

//
// NOTE: GPU Data
//

struct gpu_directional_light
{
    v3 Color;
    u32 Pad0;
    v3 Dir;
    u32 Pad1;
    v3 AmbientColor;
    u32 Pad2;
    m4 VPTransform;
};

struct gpu_instance_entry
{
    m4 WTransform;
    m4 WVPTransform;
};

struct gpu_blinn_phong_material
{
    v3 DiffuseColor;
    u32 Pad0;
    v3 SpecularColor;
    f32 SpecularPower;
};

struct scene_globals
{
    m4 InvVpTransform;
    v3 CameraPos;
    u32 NumPointLights;
};

//
// NOTE: Render Structs
//

struct shadow_data
{
    vk_linear_arena Arena;
    
    u32 Width;
    u32 Height;
    VkSampler Sampler;
    VkImage ShadowImage;
    render_target_entry ShadowEntry;
    render_target RenderTarget;

    gpu_directional_light GpuData;
    VkBuffer RenderGlobals;
    VkBuffer InstanceTransforms;
    
    VkDescriptorSet Descriptor;
};

struct instance_entry
{
    u32 HandleId;
    m4 ShadowWVP;
    m4 WTransform;
    m4 WVPTransform;
};

struct render_mesh
{
    vk_image Color;
    vk_image Normal;
    VkDescriptorSet MaterialDescriptor;

    VkBuffer VertexBuffer;
    VkBuffer IndexBuffer;
    u32 NumIndices;
};

struct render_model
{
    asset_model AssetModel;
    VkDescriptorSet* MaterialDescriptors;
};

enum render_handle_type
{
    RenderHandleType_None,

    RenderHandleType_Mesh,
    RenderHandleType_Model,
};

struct render_handle
{
    render_handle_type Type;
    union
    {
        render_mesh Mesh;
        render_model Model;
    };
};

struct render_scene
{
    vk_image WhiteTexture;
    
    // NOTE: General Render Data
    camera Camera;
    VkDescriptorSetLayout MaterialDescLayout;
    VkDescriptorSetLayout SceneDescLayout;
    VkBuffer SceneBuffer;
    VkDescriptorSet SceneDescriptor;

    // NOTE: Shadow
    VkDescriptorSetLayout ShadowDescLayout;
    vk_pipeline* ShadowPipeline;
    shadow_data ShadowData;
    
    // NOTE: Scene Lights
    u32 MaxNumPointLights;
    u32 NumPointLights;
    point_light* PointLights;
    VkBuffer PointLightBuffer;
    VkBuffer PointLightTransforms;
    
    // NOTE: Scene Meshes
    u32 MaxNumRenderHandles;
    u32 NumRenderHandles;
    render_handle* RenderHandles;

    // NOTE: Material Data
    u32 MaxNumMaterials;
    u32 NumMaterials;
    gpu_blinn_phong_material* Materials;
    VkBuffer MaterialBuffer;
    
    // NOTE: Opaque Instances
    u32 MaxNumOpaqueInstances;
    u32 NumOpaqueInstances;
    instance_entry* OpaqueInstances;
    VkBuffer OpaqueInstanceBuffer;
};

