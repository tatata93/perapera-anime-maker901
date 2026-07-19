param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "build",
    [string]$OutputDir = "dist",
    [string]$PackageName = "perapera-anime-maker901-windows-x64-portable"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
$buildRoot = Join-Path $repoRoot $BuildDir
$releaseDir = Join-Path $buildRoot "src\$Configuration"
$outputRoot = Join-Path $repoRoot $OutputDir
$packageRoot = Join-Path $outputRoot $PackageName
$zipPath = Join-Path $outputRoot "$PackageName.zip"
$hashPath = "$zipPath.sha256"
$mainExe = Join-Path $releaseDir "perapera-anime-maker901.exe"
$windeployqt = Join-Path "C:\Qt\6.9.2\msvc2022_64\bin" "windeployqt.exe"

function Assert-UnderRepo([string]$Path) {
    $resolved = Resolve-Path $Path -ErrorAction SilentlyContinue
    if ($resolved -and -not $resolved.Path.StartsWith($repoRoot.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to touch path outside repository: $($resolved.Path)"
    }
}

if (-not (Test-Path $mainExe)) {
    throw "Release build was not found: $mainExe. Run: cmake --build build --config Release"
}

if (-not (Test-Path $windeployqt)) {
    $found = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if (-not $found) {
        throw "windeployqt.exe was not found. Install Qt or update this script."
    }
    $windeployqt = $found.Source
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
Assert-UnderRepo $outputRoot
if (Test-Path $packageRoot) {
    Assert-UnderRepo $packageRoot
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
if (Test-Path $zipPath) {
    Assert-UnderRepo $zipPath
    Remove-Item -LiteralPath $zipPath -Force
}
if (Test-Path $hashPath) {
    Assert-UnderRepo $hashPath
    Remove-Item -LiteralPath $hashPath -Force
}

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null

Copy-Item -Path (Join-Path $releaseDir "*") -Destination $packageRoot -Recurse -Force

& $windeployqt --release (Join-Path $packageRoot "perapera-anime-maker901.exe")
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

$runtimeDlls = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "concrt140.dll")
foreach ($dll in $runtimeDlls) {
    if (Test-Path (Join-Path $packageRoot $dll)) {
        continue
    }

    $source = Join-Path $env:WINDIR "System32\$dll"
    if (Test-Path $source) {
        Copy-Item -LiteralPath $source -Destination $packageRoot -Force
    } else {
        Write-Warning "VC runtime DLL was not found and will not be bundled: $dll"
    }
}

@"
perapera-anime-maker901 Windows x64 portable

使い方:
1. このzipを好きな場所に展開する
2. perapera-anime-maker901.exe をダブルクリックする

起動用bat:
- START_PERAPERA.bat    : 既定UI。現在はWindows 95風
- START_NORMAL_UI.bat   : 通常UI
- START_95_UI.bat       : Windows 95風UI
- START_XP_UI.bat       : Windows XP風UI

注意:
- フォルダ内の dll や platforms などのサブフォルダは消さないでください。
- Windows 10/11 x64 向けです。
- mp4書き出しには別途 ffmpeg.exe が必要です。PNG書き出しはこのzipだけで使えます。
- Qt は LGPL ライセンスの動的リンクとして同梱しています。
"@ | Set-Content -Encoding UTF8 -Path (Join-Path $packageRoot "README_JA.txt")

@"
@echo off
cd /d "%~dp0"
start "" "perapera-anime-maker901.exe"
"@ | Set-Content -Encoding ASCII -Path (Join-Path $packageRoot "START_PERAPERA.bat")

@"
@echo off
cd /d "%~dp0"
start "" "perapera-anime-maker901.exe" --normal-ui
"@ | Set-Content -Encoding ASCII -Path (Join-Path $packageRoot "START_NORMAL_UI.bat")

@"
@echo off
cd /d "%~dp0"
start "" "perapera-anime-maker901.exe" --retro-ui=95
"@ | Set-Content -Encoding ASCII -Path (Join-Path $packageRoot "START_95_UI.bat")

@"
@echo off
cd /d "%~dp0"
start "" "perapera-anime-maker901.exe" --retro-ui=xp
"@ | Set-Content -Encoding ASCII -Path (Join-Path $packageRoot "START_XP_UI.bat")

@"
Third-party notices

This package includes Qt 6 runtime libraries, dynamically linked.
Qt is available under LGPL/GPL/commercial terms.
See: https://www.qt.io/licensing/

This package also includes runtime libraries and plugins required by Qt on Windows.
"@ | Set-Content -Encoding UTF8 -Path (Join-Path $packageRoot "THIRD_PARTY_NOTICES.txt")

Compress-Archive -Path $packageRoot -DestinationPath $zipPath -CompressionLevel Optimal

$hash = Get-FileHash -Algorithm SHA256 -Path $zipPath
"$($hash.Hash)  $(Split-Path -Leaf $zipPath)" | Set-Content -Encoding ASCII -Path $hashPath

Assert-UnderRepo $packageRoot
Remove-Item -LiteralPath $packageRoot -Recurse -Force

Write-Host "Created $zipPath"
Write-Host "Created $hashPath"
