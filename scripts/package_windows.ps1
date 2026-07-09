<#
.SYNOPSIS
  package_windows.ps1 — 建置 macpad++、以 windeployqt 同梱 Qt/WebEngine/QScintilla，
  打包成可在乾淨 Windows 執行的免安裝包（zip）。

.DESCRIPTION
  對應 docs/windows_sa_sd.md §5、windows_design.md §8。
  需先在 MSVC 開發者環境（vcvars64）中執行，且 Qt 的 bin 在 PATH。

.PARAMETER Version
  版本號（不含 v 前綴），供輸出檔名使用。

.PARAMETER Arch
  架構後綴（預設 x64）。

.PARAMETER QtPrefix
  Qt 安裝前綴（預設 C:\Qt\6.8.1\msvc2022_64，可用環境變數 QTDIR 覆寫）。

.EXAMPLE
  pwsh scripts/package_windows.ps1 -Version 0.4.0
  產出：dist\macpad++-0.4.0-x64.zip
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Arch = "x64",
    [string]$QtPrefix = $(if ($env:QTDIR) { $env:QTDIR } else { "C:\Qt\6.8.1\msvc2022_64" })
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$windeployqt = Join-Path $QtPrefix "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) { throw "找不到 windeployqt：$windeployqt（請確認 QtPrefix）" }

$buildDir = "build-release"
$dist = "dist"
$stage = Join-Path $dist "macpad++"

Write-Host "==> 設定並建置 Release ($Arch)"
cmake -S . -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QtPrefix"
if ($LASTEXITCODE) { throw "cmake configure 失敗" }
cmake --build $buildDir -j
if ($LASTEXITCODE) { throw "cmake build 失敗" }

$exe = Join-Path $buildDir "src\macpad++.exe"
if (-not (Test-Path $exe)) { throw "建置後找不到 $exe" }

Write-Host "==> 建立 staging 目錄"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe $stage

Write-Host "==> windeployqt 同梱 Qt 相依（含 WebEngine 執行元件）"
& $windeployqt --release --compiler-runtime (Join-Path $stage "macpad++.exe")
if ($LASTEXITCODE) { throw "windeployqt 失敗" }

Write-Host "==> 補上 QScintilla 執行期 DLL"
$qsciDll = @(
    (Join-Path $QtPrefix "bin\qscintilla2_qt6.dll"),
    (Join-Path $QtPrefix "lib\qscintilla2_qt6.dll")
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $qsciDll) { throw "找不到 qscintilla2_qt6.dll（於 $QtPrefix bin/lib）" }
Copy-Item $qsciDll $stage

Write-Host "==> 驗證關鍵檔存在"
$required = @(
    "macpad++.exe",
    "qscintilla2_qt6.dll",
    "Qt6Core.dll",
    "Qt6Widgets.dll",
    "Qt6WebEngineWidgets.dll",
    "QtWebEngineProcess.exe",
    "platforms\qwindows.dll"
)
$missing = @()
foreach ($r in $required) {
    if (-not (Test-Path (Join-Path $stage $r))) { $missing += $r }
}
if ($missing.Count) {
    throw "封裝缺少關鍵檔：`n  " + ($missing -join "`n  ")
}
Write-Host "   OK：關鍵檔齊備"

Write-Host "==> 產生 zip"
$zip = Join-Path $dist "macpad++-$Version-$Arch.zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force
Write-Host "==> 完成：$zip"
Get-Item $zip | Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } }
