[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$Runtime = "win-x64",
    [string]$OutputFolder = "dist-portable"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repoRoot "FileCompressor.Wpf.csproj"
$publishDir = Join-Path $repoRoot $OutputFolder
$zipPath = Join-Path $repoRoot "FileCompressor-portable-$Runtime.zip"

function Copy-ToolFromPath {
    param([Parameter(Mandatory)][string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command -or [string]::IsNullOrWhiteSpace($command.Source) -or -not (Test-Path $command.Source)) {
        Write-Warning "$Name was not found on PATH. Media compression needs $Name in the portable folder."
        return $false
    }

    Copy-Item -LiteralPath $command.Source -Destination (Join-Path $publishDir $Name) -Force
    Write-Host "Bundled $Name from $($command.Source)"

    $toolFolder = Split-Path -Parent $command.Source
    Get-ChildItem -LiteralPath $toolFolder -Filter "*.dll" -File -ErrorAction SilentlyContinue |
        Copy-Item -Destination $publishDir -Force

    return $true
}

if (Test-Path $publishDir) {
    Remove-Item -LiteralPath $publishDir -Recurse -Force
}

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

dotnet publish $projectPath `
    -c $Configuration `
    -r $Runtime `
    --self-contained true `
    -o $publishDir `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:PublishReadyToRun=true

$hasFfmpeg = Copy-ToolFromPath "ffmpeg.exe"
$hasFfprobe = Copy-ToolFromPath "ffprobe.exe"

$notes = @"
File Compressor portable

How to use this on another Windows computer:

1. Extract the whole ZIP folder.
2. Open FileCompressor.exe.
3. Add a video or file and choose an output folder.

No .NET install should be needed because the runtime is included in this folder.

For video, image, and audio compression, this folder needs:

- ffmpeg.exe
- ffprobe.exe

Bundled by this script:

- ffmpeg.exe: $hasFfmpeg
- ffprobe.exe: $hasFfprobe

If either one says False, install FFmpeg on this computer or copy ffmpeg.exe and ffprobe.exe into this folder before taking it somewhere else.

Some school or work computers block unknown apps. If that happens, the app may need permission from the computer admin.
"@

Set-Content -LiteralPath (Join-Path $publishDir "README-RUN-FIRST.txt") -Value $notes -Encoding UTF8

Compress-Archive -Path (Join-Path $publishDir "*") -DestinationPath $zipPath -Force

Write-Host ""
Write-Host "Portable folder: $publishDir"
Write-Host "Portable ZIP:    $zipPath"
