#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#define MATH_GLSL 1
#include "../libs/math/math.h"

struct vol_fog_buffer
{
    v3 Albedo; // NOTE: Probability of scattering (in/out), basically the color of the fog
    f32 GScattering; // NOTE: Anisotropy, probability of forward scattering
    v3 FogMinPos;
    f32 Density; // NOTE: Probability of being absorbed
    v3 FogMaxPos;
    u32 Pad;
    v3 FogNumTexels;
    f32 CurrFrameTime;
};

layout(set = 0, binding = 0) uniform vol_fog_buffer_uniform
{
    vol_fog_buffer VolFogBuffer;
};

layout(set = 0, binding = 1, r32f) uniform image3D FogTextureUav;

//=========================================================================================================================================
// NOTE: Generate 3D Fog Texture
//=========================================================================================================================================

#if GENERATE_3D_FOG

// TODO: Reuse? Derivation?
float hash1(float n)
{
    return fract( n*17.0*fract( n*0.3183099 ) );
}

// TODO: Reuse? Derivation?
float noise(in vec3 x)
{
    vec3 p = floor(x);
    vec3 w = fract(x);
    
    vec3 u = w*w*w*(w*(w*6.0-15.0)+10.0);
    
    float n = p.x + 317.0*p.y + 157.0*p.z;
    
    float a = hash1(n+0.0);
    float b = hash1(n+1.0);
    float c = hash1(n+317.0);
    float d = hash1(n+318.0);
    float e = hash1(n+157.0);
    float f = hash1(n+158.0);
    float g = hash1(n+474.0);
    float h = hash1(n+475.0);

    float k0 =   a;
    float k1 =   b - a;
    float k2 =   c - a;
    float k3 =   e - a;
    float k4 =   a - b - c + d;
    float k5 =   a - c - e + g;
    float k6 =   a - b - e + f;
    float k7 = - a + b + c - d + e - f - g + h;

    return -1.0+2.0*(k0 + k1*u.x + k2*u.y + k3*u.z + k4*u.x*u.y + k5*u.y*u.z + k6*u.z*u.x + k7*u.x*u.y*u.z);
}

f32 fbm_4(v3 x)
{
    const mat3 Transform  = mat3( 0.00,  0.80,  0.60,
                                  -0.80,  0.36, -0.48,
                                  -0.60, -0.48,  0.64 );
    
    f32 f = 2.0;
    f32 s = 0.5;
    f32 a = 0.0;
    f32 b = 0.5;
    
    for(u32 i = 0; i < 2; i++)
    {
        f32 n = noise(x);
        a += b*n;
        b *= s;
        x = f*Transform*x;
    }
    return a;
}

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
void main()
{
    // NOTE: Generate a height fog
    v3 T = gl_GlobalInvocationID.xyz / VolFogBuffer.FogNumTexels;
    f32 FogDensity = 0.0f;

    // NOTE: Simulated noise
#if 0
    {
        // TODO: Very rough, im going for a perlin noise but I got 2 issues I can't resolve
        // The fbm_4 stuff adds a nice effect but its hard to manage with SDFs for me.
        f32 SlowedFrameTime = 1.25f * VolFogBuffer.CurrFrameTime + 4.0f;
        
        // NOTE: A couple moving spheres
        f32 Distance = SdfSphere(SdfTranslate(T, V3(0.3f, 0.2f + 0.5f * Sin(SlowedFrameTime), 0.2f)), 0.3f);
        Distance = SdfSmoothUnion(Distance, SdfSphere(SdfTranslate(T, V3(0.5f + 0.2f * Cos(SlowedFrameTime), 0.2f, 0.2f)), 0.4f), 0.2f);
        Distance = SdfSmoothUnion(Distance, SdfSphere(SdfTranslate(T, V3(0.5f + 0.3f * Cos(SlowedFrameTime), 0.8f, 0.1f)), 0.2f), 0.1f);
        Distance = SdfSmoothUnion(Distance, SdfSphere(SdfTranslate(T, V3(0.4f, 0.7f, Clamp(0.1f + 0.2f*Sin(SlowedFrameTime), 0.0f, 1.0f))), 0.2f), 0.3f);
        Distance += 1.0f*fbm_4(T + 0.5f * V3(SlowedFrameTime));

        // NOTE: Make the fog darker deeper
        f32 A = 16.0f;
        f32 B = 5.0f;

        FogDensity = Distance < 0.0f ? 1.0f : 0.0f;
        //FogDensity = Distance < 0.0f ? Clamp(A * Exp(-B * Distance), 0.0f, 1.0f) : 0.0f;
    }
#endif
    
    // NOTE: Height based Fog
#if 1
    {
        f32 A = 5.0f;
        f32 B = 5.0f;

        //A += 10.0f * Clamp(fbm_4(V3(T.xy / 4.0f + V2(VolFogBuffer.CurrFrameTime, 0.0f), 0.0f)), 0.0f, 1.0f);
        
        FogDensity = A * Exp(-B * T.z);
    }
#endif
    
    // TODO: Add more noise + animation?
    imageStore(FogTextureUav, V3I(gl_GlobalInvocationID.xyz), V4(FogDensity));
}

#endif
