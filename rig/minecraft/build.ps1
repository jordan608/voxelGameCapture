<#
Builds the Minecraft rig (a Fabric mod) and puts it in ..\..\redist as VoxonCaptureRig.Minecraft.jar, where the app's
installer takes it from. Its decode host, voxon_capture_mc.exe, is built with the add-on (addon\build.ps1 -Redist).

    rig\minecraft\build.ps1
    rig\minecraft\build.ps1 -JavaHome 'D:\Tools\jdk-25.0.4.1+1'

Needs a JDK 25 (JAVA_HOME, or -JavaHome) and Gradle's wrapper, which is not in git: run "gradle wrapper
--gradle-version 9.7.1" here once (docs/DEVELOPER_GUIDE.md). The first build downloads Fabric Loom and the game's own
jar, to compile against; nothing of the game's goes into the mod.
#>
[CmdletBinding()]
param([string]$JavaHome = $env:JAVA_HOME)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not $JavaHome -or -not (Test-Path (Join-Path $JavaHome 'bin\java.exe'))) { throw "no JDK: set JAVA_HOME, or pass -JavaHome (JDK 25)" }
if (-not (Test-Path 'gradlew.bat')) { throw "no Gradle wrapper: run 'gradle wrapper --gradle-version 9.7.1' here once" }
$env:JAVA_HOME = $JavaHome

.\gradlew.bat build -q
if ($LASTEXITCODE -ne 0) { throw "build failed" }
$jar = Get-ChildItem 'build\libs' -Filter 'VoxonCaptureRig.Minecraft-*.jar' | Where-Object { $_.Name -notmatch 'sources' } |
       Sort-Object LastWriteTime | Select-Object -Last 1
Copy-Item $jar.FullName (Join-Path $PSScriptRoot '..\..\redist\VoxonCaptureRig.Minecraft.jar') -Force
Write-Host "built and copied to redist\VoxonCaptureRig.Minecraft.jar" -ForegroundColor Green
