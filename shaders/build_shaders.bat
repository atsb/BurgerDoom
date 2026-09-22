@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "ROOT=%~dp0"
set "OUT=%ROOT%out"
if not exist "%OUT%" mkdir "%OUT%"

set "GLSLC="
set "DXC="

if defined VULKAN_SDK (
    if exist "%VULKAN_SDK%\Bin\glslc.exe" set "GLSLC=%VULKAN_SDK%\Bin\glslc.exe"
    if exist "%VULKAN_SDK%\Bin\dxc.exe" set "DXC=%VULKAN_SDK%\Bin\dxc.exe"
)

if not defined GLSLC if defined VK_SDK_PATH (
    if exist "%VK_SDK_PATH%\Bin\glslc.exe" set "GLSLC=%VK_SDK_PATH%\Bin\glslc.exe"
)

if not defined DXC if defined VK_SDK_PATH (
    if exist "%VK_SDK_PATH%\Bin\dxc.exe" set "DXC=%VK_SDK_PATH%\Bin\dxc.exe"
)

if not defined GLSLC (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%ProgramFiles%\VulkanSDK" 2^>nul') do (
        if not defined GLSLC if exist "%ProgramFiles%\VulkanSDK\%%D\Bin\glslc.exe" set "GLSLC=%ProgramFiles%\VulkanSDK\%%D\Bin\glslc.exe"
    )
)

if not defined DXC (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%ProgramFiles%\VulkanSDK" 2^>nul') do (
        if not defined DXC if exist "%ProgramFiles%\VulkanSDK\%%D\Bin\dxc.exe" set "DXC=%ProgramFiles%\VulkanSDK\%%D\Bin\dxc.exe"
    )
)

if not defined GLSLC (
    for /f "delims=" %%P in ('where glslc.exe 2^>nul') do if not defined GLSLC set "GLSLC=%%P"
)

if not defined DXC if defined DXC_PATH if exist "%DXC_PATH%" set "DXC=%DXC_PATH%"

if not defined DXC (
    for /f "delims=" %%P in ('where dxc.exe 2^>nul') do if not defined DXC set "DXC=%%P"
)

if not defined GLSLC (
    echo.
    echo glslc.exe was not found.
    echo Checked VULKAN_SDK, VK_SDK_PATH, C:\VulkanSDK, and PATH.
    echo.
    echo Current VULKAN_SDK: %VULKAN_SDK%
    echo Current VK_SDK_PATH: %VK_SDK_PATH%
    exit /b 1
)

if not defined DXC (
    echo.
    echo dxc.exe was not found.
    echo Checked VULKAN_SDK, VK_SDK_PATH, C:\VulkanSDK, DXC_PATH, and PATH.
    echo.
    echo Current VULKAN_SDK: %VULKAN_SDK%
    echo Current VK_SDK_PATH: %VK_SDK_PATH%
    exit /b 1
)

echo Using:
echo   GLSLC: %GLSLC%
echo   DXC:   %DXC%
echo.

echo Compiling Vulkan SPIR-V...
"%GLSLC%" -fshader-stage=frag -O -o "%OUT%\doom3do.spv" "%ROOT%shaders\doom3do.frag.glsl"
if errorlevel 1 exit /b 1

echo Compiling Direct3D 12 DXIL...
"%DXC%" -T ps_6_0 -E main -O3 -Fo "%OUT%\doom3do.dxil" "%ROOT%shaders\doom3do.frag.hlsl"
if errorlevel 1 exit /b 1

echo.
echo Built:
echo   %OUT%\doom3do.spv
echo   %OUT%\doom3do.dxil
exit /b 0
