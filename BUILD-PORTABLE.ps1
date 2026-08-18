param(
    [switch]$SkipMsysInstall
)
$ErrorActionPreference = "Stop"
$Target = "win10"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$MsysRoot = "C:\msys64"
$Bash = Join-Path $MsysRoot "usr\bin\bash.exe"

Write-Host "S.I.P.H.E.R. By GITSC - Windows portable builder ($Target)" -ForegroundColor Cyan

if (-not (Test-Path $Bash)) {
    if ($SkipMsysInstall) { throw "MSYS2 is not installed at C:\msys64." }
    Write-Host "MSYS2 was not found. Downloading the official MSYS2 self-extracting build environment..." -ForegroundColor Yellow
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $Installer = Join-Path $env:TEMP "msys2-base-x86_64-latest.sfx.exe"
    $Url = "https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.sfx.exe"
    Invoke-WebRequest -Uri $Url -OutFile $Installer -UseBasicParsing
    & $Installer -y -oC:\
    if ($LASTEXITCODE -ne 0) { throw "MSYS2 extraction failed with exit code $LASTEXITCODE" }
    Remove-Item $Installer -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path $Bash)) { throw "MSYS2 bash was not found after bootstrap." }

# Refresh package metadata/base packages. A current build host is intentionally used
# even for the Win7 target; the produced application is the compatibility target.
Write-Host "Updating MSYS2 package metadata..." -ForegroundColor Cyan
& $Bash -lc "pacman -Sy --noconfirm"
if ($LASTEXITCODE -ne 0) { throw "MSYS2 package refresh failed." }

Write-Host "Building S.I.P.H.E.R. $Target portable GUI + CLI..." -ForegroundColor Green
Push-Location $Root
try {
    & (Join-Path $Root "build-windows-portable.cmd") $Target
    if ($LASTEXITCODE -ne 0) { throw "S.I.P.H.E.R. Windows build failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }

Write-Host "Build complete. See the dist folder." -ForegroundColor Green
