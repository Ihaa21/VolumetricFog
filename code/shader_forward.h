
#include "shader_scene_structs.h"

//
// NOTE: Material
//

layout(set = 0, binding = 0) uniform sampler2D ColorTexture;
layout(set = 0, binding = 1) uniform sampler2D NormalTexture;
layout(set = 0, binding = 2) buffer material_buffer        
{                                                                   
    blinn_phong_material MaterialBuffer;
};                                                                  

//
// NOTE: Scene
//

layout(set = 1, binding = 0) uniform scene_buffer          
{
    scene_globals SceneBuffer;
};                                                      
                                                                        
layout(set = 1, binding = 1) buffer instance_buffer        
{                                                                   
    instance_entry InstanceBuffer[];                                
};                                                                  
                                                                        
layout(set = 1, binding = 2) buffer point_light_buffer     
{                                                                   
    point_light PointLights[];                                      
};                                                                  
                                                                        
layout(set = 1, binding = 3) buffer point_light_transforms 
{                                                                   
    m4 PointLightTransforms[];                                    
};                                                                  
                                                                        
layout(set = 1, binding = 4) buffer directional_light_buffer 
{                                                                   
    directional_light_gpu DirectionalLight;                         
};                                                                  

layout(set = 1, binding = 5) buffer directional_shadow_transforms 
{
    m4 DirectionalTransforms[];                                   
};

layout(set = 2, binding = 0) uniform sampler2D StandardShadowMap;
    

