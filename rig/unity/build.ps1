<#
Builds the Unity rig (a BepInEx 5 plugin) and puts it in ..\..\redist, where the app's installer takes it from.

    rig\unity\build.ps1
    rig\unity\build.ps1 -UnityManaged '<game>\<game>_Data\Managed'    # against another Unity game's assemblies

It compiles against a Unity game's own assemblies (Risk of Rain 2's by default) and BepInEx's, taken out of the
BepInEx zip in redist\. Needs the .NET SDK and the .NET Framework 4.6 reference assemblies (Visual Studio).
#>
[CmdletBinding()]
param([string]$UnityManaged)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$redist = Resolve-Path (Join-Path $PSScriptRoot '..\..\redist')
$zip = Get-ChildItem $redist -Filter 'BepInEx_win_x64_*.zip' | Select-Object -First 1
if (-not $zip) { throw "no BepInEx zip in $redist" }

New-Item -ItemType Directory -Force lib | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [System.IO.Compression.ZipFile]::OpenRead($zip.FullName)
try {
  $e = $z.Entries | Where-Object { $_.FullName -eq 'BepInEx/core/BepInEx.dll' }
  [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, (Join-Path $PSScriptRoot 'lib\BepInEx.dll'), $true)
} finally { $z.Dispose() }

$buildArgs = @('build', 'VoxonCaptureRig.Unity.csproj', '-c', 'Release', '-nologo', '-v', 'q')
if ($UnityManaged) { $buildArgs += "-p:UnityManaged=$UnityManaged" }
dotnet @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Copy-Item 'bin\Release\net46\VoxonCaptureRig.Unity.dll' $redist -Force
Write-Host "built and copied to redist\VoxonCaptureRig.Unity.dll" -ForegroundColor Green
