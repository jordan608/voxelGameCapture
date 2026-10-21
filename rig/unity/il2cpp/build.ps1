<#
Builds the Unity rig for IL2CPP games (a BepInEx 6 plugin, from ..\'s sources) and puts it in ..\..\..\redist, where
the app's installer takes it from.

    rig\unity\il2cpp\build.ps1
    rig\unity\il2cpp\build.ps1 -GameRoot '<an IL2CPP game's folder>'    # against another game

An IL2CPP game has no managed assemblies to build against: it builds against a game where BepInEx 6 has run once
(Gang Beasts' by default), whose BepInEx\interop holds the interop assemblies BepInEx made, and whose dotnet\ is the
.NET the plugin runs on. Needs the .NET SDK.
#>
[CmdletBinding()]
param([string]$GameRoot)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$redist = Resolve-Path (Join-Path $PSScriptRoot '..\..\..\redist')

$buildArgs = @('build', 'VoxonCaptureRig.Unity.IL2CPP.csproj', '-c', 'Release', '-nologo', '-v', 'q')
if ($GameRoot) { $buildArgs += "-p:GameRoot=$GameRoot" }
dotnet @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Copy-Item 'bin\Release\net6.0\VoxonCaptureRig.Unity.IL2CPP.dll' $redist -Force
Write-Host "built and copied to redist\VoxonCaptureRig.Unity.IL2CPP.dll" -ForegroundColor Green
