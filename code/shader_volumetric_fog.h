
struct vol_fog_buffer
{
    v3 Albedo; // NOTE: Probability of scattering (in/out), basically the color of the fog
    f32 GScattering; // NOTE: Anisotropy, probability of forward scattering
    v3 FogMinPos;
    f32 Density; // NOTE: Probability of being absorbed
    v3 FogMaxPos;
    u32 Pad;
    v3 FogNumTexels;
};

layout(set = 0, binding = 0) uniform vol_fog_buffer_uniform
{
    vol_fog_buffer VolFogBuffer;
};

layout (input_attachment_index = 0, set = 0, binding = 1) uniform subpassInput Color;
layout (input_attachment_index = 1, set = 0, binding = 2) uniform subpassInput Normal;
layout (input_attachment_index = 2, set = 0, binding = 3) uniform subpassInput Depth;

layout(set = 0, binding = 4) uniform sampler2D Shadow;

layout(set = 0, binding = 5) uniform sampler3D FogTextureFiltered;

layout(set = 1, binding = 0) uniform scene_buffer          
{
    scene_globals SceneBuffer;
};                                                      
                                                                        
layout(set = 1, binding = 2) buffer point_light_buffer     
{                                                                   
    point_light PointLights[];                                      
};                                                                  
                                                                        
layout(set = 1, binding = 4) buffer directional_light_buffer 
{                                                                   
    directional_light_gpu DirectionalLight;                         
};                                                                  
