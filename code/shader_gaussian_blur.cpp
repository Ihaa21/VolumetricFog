#version 450

/*

  TODO: References for making a faster version:

    - https://rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/
    - https://fgiesen.wordpress.com/2012/07/30/fast-blurs-1/
  
 */

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout(binding = 0, set = 0) uniform sampler2D InputTexture;

#if GAUSSIAN_BLUR_X

layout(location = 0) out vec2 OutMoments;

void main()
{
    ivec2 PixelCoord = ivec2(gl_FragCoord.xy);
    
    // NOTE: https://graphics.stanford.edu/~mdfisher/Code/ShadowMap/GaussianBlurX.ps.html
    vec2 Output = vec2(0);
    float Coefficients[21] = 
        {
            0.000272337, 0.00089296, 0.002583865, 0.00659813, 0.014869116,
            0.029570767, 0.051898313, 0.080381679, 0.109868729, 0.132526984,
            0.14107424, 0.132526984, 0.109868729, 0.080381679, 0.051898313,
            0.029570767, 0.014869116, 0.00659813, 0.002583865, 0.00089296,
            0.000272337
        };

    for (int TexelId = 0; TexelId < 21; ++TexelId)
    {
        Output += texelFetch(InputTexture, PixelCoord + ivec2(TexelId - 10, 0), 0).xy * Coefficients[TexelId];
    }

    OutMoments = Output;
}

#endif

#if GAUSSIAN_BLUR_Y

layout(location = 0) out vec2 OutMoments;

void main()
{
    ivec2 PixelCoord = ivec2(gl_FragCoord.xy);
    
    // NOTE: https://graphics.stanford.edu/~mdfisher/Code/ShadowMap/GaussianBlurY.ps.html
    vec2 Output = vec2(0);
    float Coefficients[21] = 
        {
            0.000272337, 0.00089296, 0.002583865, 0.00659813, 0.014869116,
            0.029570767, 0.051898313, 0.080381679, 0.109868729, 0.132526984,
            0.14107424, 0.132526984, 0.109868729, 0.080381679, 0.051898313,
            0.029570767, 0.014869116, 0.00659813, 0.002583865, 0.00089296,
            0.000272337
        };

    for (int TexelId = 0; TexelId < 21; ++TexelId)
    {
        Output += texelFetch(InputTexture, PixelCoord + ivec2(0, TexelId - 10), 0).xy * Coefficients[TexelId];
    }

    OutMoments = Output;
}

#endif
