<#
Builds the folder a player gets: dist\VoxonCaptureRig, with the app, everything it installs into games, and the .NET
runtime, so the PC it is copied to needs nothing else (bar the Voxon VLED runtime, which is never bundled).

    tools\package.ps1              # the app, then its self-test
    tools\package.ps1 -Addon       # rebuild the GPU add-on first (needs Visual Studio's C++ tools)
#>
[CmdletBinding()]
param([switch]$Addon)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'dist\VoxonCaptureRig'

if ($Addon) { & (Join-Path $root 'addon\build.ps1') -Redist }

if (Test-Path $out) { Remove-Item $out -Recurse -Force }
dotnet publish (Join-Path $root 'host\VoxonCaptureRig\VoxonCaptureRig.csproj') -c Release -r win-x64 --self-contained -o $out -nologo
if ($LASTEXITCODE -ne 0) { throw "publish failed" }

& (Join-Path $out 'VoxonCaptureRig.exe') --selftest | Out-Null
$ok = $LASTEXITCODE -eq 0
Get-Content (Join-Path $out 'selftest.txt') | Select-Object -Last 1
Remove-Item (Join-Path $out 'selftest.txt') -ErrorAction SilentlyContinue
if (-not $ok) { throw "self-test FAILED" }
Write-Host "packaged: $out" -ForegroundColor Green
