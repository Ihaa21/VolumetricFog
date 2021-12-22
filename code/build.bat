@echo off

set CodeDir=..\code
set DataDir=..\data
set LibsDir=..\libs
set OutputDir=..\build_win32
set VulkanIncludeDir="C:\VulkanSDK\1.2.198.0\Include\vulkan"
set VulkanBinDir="C:\VulkanSDK\1.2.198.0\Bin"
set AssimpDir=%LibsDir%\framework_vulkan

set CommonCompilerFlags=-Od -MTd -nologo -fp:fast -fp:except- -EHsc -Gm- -GR- -EHa- -Zo -Oi -WX -W4 -wd4127 -wd4201 -wd4100 -wd4189 -wd4505 -Z7 -FC
set CommonCompilerFlags=-I %VulkanIncludeDir% %CommonCompilerFlags%
set CommonCompilerFlags=-I %LibsDir% -I %AssimpDir% %CommonCompilerFlags%
REM Check the DLLs here
set CommonLinkerFlags=-incremental:no -opt:ref user32.lib gdi32.lib Winmm.lib opengl32.lib DbgHelp.lib d3d12.lib dxgi.lib d3dcompiler.lib %AssimpDir%\assimp\libs\assimp-vc142-mt.lib

IF NOT EXIST %OutputDir% mkdir %OutputDir%

pushd %OutputDir%

del *.pdb > NUL 2> NUL

REM USING GLSL IN VK USING GLSLANGVALIDATOR
call glslangValidator -DSHADOW_VERTEX=1 -S vert -e main -g -V -o %DataDir%\shader_shadow_vert.spv %CodeDir%\shader_forward.cpp

call glslangValidator -DFORWARD_VERTEX=1 -DSTANDARD=1 -S vert -e main -g -V -o %DataDir%\shader_forward_standard_vert.spv %CodeDir%\shader_forward.cpp
call glslangValidator -DFORWARD_FRAGMENT=1 -DSTANDARD=1 -S frag -e main -g -V -o %DataDir%\shader_forward_standard_frag.spv %CodeDir%\shader_forward.cpp

call glslangValidator -DRAYMARCH_FOG_FRAG=1 -S frag -e main -g -V -o %DataDir%\shader_raymarch_fog_frag.spv %CodeDir%\shader_volumetric_fog.cpp

call glslangValidator -DRAYMARCH_FOG_3D_FRAG=1 -S frag -e main -g -V -o %DataDir%\shader_raymarch_fog_3d_frag.spv %CodeDir%\shader_volumetric_fog.cpp
call glslangValidator -DGENERATE_3D_FOG=1 -S comp -e main -g -V -o %DataDir%\shader_generate_3d_fog.spv %CodeDir%\shader_volumetric_3d.cpp

call glslangValidator -DRAYMARCH_SMOKE=1 -S frag -e main -g -V -o %DataDir%\shader_raymarch_smoke_frag.spv %CodeDir%\shader_volumetric_smoke.cpp

call glslangValidator -DGAUSSIAN_BLUR_X=1 -S frag -e main -g -V -o %DataDir%\shader_gaussian_x_frag.spv %CodeDir%\shader_gaussian_blur.cpp
call glslangValidator -DGAUSSIAN_BLUR_Y=1 -S frag -e main -g -V -o %DataDir%\shader_gaussian_y_frag.spv %CodeDir%\shader_gaussian_blur.cpp

REM USING HLSL IN VK USING DXC
REM set DxcDir=C:\Tools\DirectXShaderCompiler\build\Debug\bin
REM %DxcDir%\dxc.exe -spirv -T cs_6_0 -E main -fspv-target-env=vulkan1.1 -Fo ..\data\write_cs.o -Fh ..\data\write_cs.o.txt ..\code\bw_write_shader.cpp

REM ASSIMP
copy %AssimpDir%\assimp\bin\assimp-vc142-mt.dll %OutputDir%\assimp-vc142-mt.dll

REM 64-bit build
echo WAITING FOR PDB > lock.tmp
cl %CommonCompilerFlags% %CodeDir%\volumetric_fog_demo.cpp -Fmvolumetric_fog_demo.map -LD /link %CommonLinkerFlags% -incremental:no -opt:ref -PDB:volumetric_fog_demo_%random%.pdb -EXPORT:Init -EXPORT:Destroy -EXPORT:SwapChainChange -EXPORT:CodeReload -EXPORT:MainLoop
del lock.tmp
call cl %CommonCompilerFlags% -DDLL_NAME=volumetric_fog_demo -Fevolumetric_fog_demo.exe %LibsDir%\framework_vulkan\win32_main.cpp -Fmvolumetric_fog_demo.map /link %CommonLinkerFlags%

popd
