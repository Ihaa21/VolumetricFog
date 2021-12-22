
struct blinn_phong_material
{
    v3 DiffuseColor;
    v3 SpecularColor;
    f32 SpecularPower;
};

struct directional_light_gpu
{
    v3 Color;
    v3 Dir;
    v3 AmbientLight;
    m4 VPTransform;
};

struct instance_entry
{
    m4 WTransform;
    m4 WVPTransform;
};

struct scene_globals
{
    m4 InvVpTransform;
    v3 CameraPos;
    u32 NumPointLights;
    v2 RenderDim;
    f32 CurrFrameTime;
    u32 CurrFrameId;
};
