<#
Build the GPU half of the re-hosted VLED capture: the ReShade add-on, its no-game self-test, and Minecraft's decode
host (voxon_capture_mc.exe: the same decoder, out of the game, for rig/minecraft).

    addon/build.ps1            # shaders, add-on, self-test, then run the self-test
    addon/build.ps1 -NoTest
    addon/build.ps1 -Redist    # and copy the add-on and the Minecraft host to ..\redist, where the installer takes them from

The two compute shaders are compiled with fxc (Windows SDK) into headers under obj\ and embedded,
because outside Unreal nothing compiles .usf at run time. The add-on writes the wire format through
feed/Feed.cpp, copied from VoxelSteamTap with the rest of this folder.

ReShade's add-on SDK headers are vendored in reshade-sdk/ (v6.7.3, see VENDORED.md), the same version
as redist/ReShade64.dll, which the host's installer puts in the game.
#>
[CmdletBinding()]
param([switch]$NoTest, [switch]$Redist)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$sdk  = Join-Path $PSScriptRoot 'reshade-sdk'
$feed = Join-Path $PSScriptRoot 'feed'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "no MSVC toolchain; install 'Desktop development with C++'" }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

New-Item -ItemType Directory -Force obj, bin | Out-Null

# /W4 /WX on our own files: a sign error here is silent on hardware and reads as an art bug.
$fxc = "fxc /nologo /T cs_5_0 /E MainCS /O3 /WX /Fh obj\pack_cs.h /Vn g_packCS shaders\pack.hlsl" +
       " && fxc /nologo /T cs_5_0 /E MainCS /O3 /WX /D DEPTH_BUFFER=1 /Fh obj\pack_depth_cs.h /Vn g_packDepthCS shaders\pack.hlsl" +
       " && fxc /nologo /T cs_5_0 /E MainCS /O3 /WX /D DEPTH_BUFFER=1 /D FROSTBITE=1 /Fh obj\pack_frostbite_cs.h /Vn g_packFrostbiteCS shaders\pack.hlsl" +
       " && fxc /nologo /T cs_5_0 /E MainCS /O3 /WX /D SURFACE_COLOUR=1 /Fh obj\pack_surface_cs.h /Vn g_packSurfaceCS shaders\pack.hlsl" +
       " && fxc /nologo /T cs_5_0 /E MainCS /O3 /WX /Fh obj\compact_cs.h /Vn g_compactCS shaders\compact.hlsl" +
       " && fxc /nologo /T cs_5_0 /E MainCS /O3 /WX /Fh obj\probe_cs.h /Vn g_probeCS shaders\probe.hlsl"
# /MT: the add-on is loaded into a game that knows nothing about it and must not need a redistributable.
$addon = "cl /nologo /LD /EHsc /O2 /std:c++17 /MT /W3 /DNDEBUG /DNOMINMAX /I`"$sdk`" /I`"$feed`" " +
         "/Fo`"obj\\`" voxon_capture_ue.cpp `"$feed\Feed.cpp`" /link /DLL /OUT:bin\voxon_capture_ue.addon64 d3d11.lib d3d12.lib user32.lib"
$test  = "cl /nologo /EHsc /O2 /std:c++17 /W4 /WX /DNOMINMAX /Fo`"obj\\`" /Fe:bin\selftest_gpu.exe selftest_gpu.cpp d3d11.lib d3d12.lib dxgi.lib"
$mc    = "cl /nologo /EHsc /O2 /std:c++17 /MT /W3 /DNDEBUG /DNOMINMAX /I`"$feed`" /Fo`"obj\\`" /Fe:bin\voxon_capture_mc.exe " +
         "minecraft_host.cpp `"$feed\Feed.cpp`" d3d11.lib user32.lib"

$prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
cmd /c "`"$vcvars`" >nul 2>&1 && $fxc && $addon && $test && $mc"
$rc = $LASTEXITCODE
$ErrorActionPreference = $prev
if ($rc -ne 0) { throw "build failed ($rc)" }
Write-Host "built bin\voxon_capture_ue.addon64, bin\selftest_gpu.exe and bin\voxon_capture_mc.exe" -ForegroundColor Green

if (-not $NoTest) {
  & .\bin\selftest_gpu.exe
  if ($LASTEXITCODE -ne 0) { throw "GPU self-test FAILED" }
}
if ($Redist) {
  Copy-Item bin\voxon_capture_ue.addon64, bin\voxon_capture_mc.exe ..\redist\ -Force
  Write-Host "copied to ..\redist\: voxon_capture_ue.addon64, voxon_capture_mc.exe" -ForegroundColor Green
}
