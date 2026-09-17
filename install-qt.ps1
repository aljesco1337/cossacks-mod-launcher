$ErrorActionPreference = "Stop"

$QtVersion = "6.11.2"
$QtArch = "win64_msvc2022_64"
$QtRoot = "C:\Qt"
$QtPath = "$QtRoot\$QtVersion\msvc2022_64"

# aqtinstall revision with Qt 6.11 support
$AqtRevision = "9e49c82edc6d946db376dec907cca5b4b486eec5"
$AqtUrl = "https://github.com/miurahr/aqtinstall/archive/$AqtRevision.zip"

Write-Host "Installing aqtinstall..." -ForegroundColor Cyan

py -m pip install --upgrade $AqtUrl

if ($LASTEXITCODE -ne 0) {
    throw "Failed to install aqtinstall."
}

Write-Host "Installing Qt $QtVersion MSVC 2022 64-bit..." -ForegroundColor Cyan

py -m aqt install-qt `
    -O $QtRoot `
    windows desktop `
    $QtVersion `
    $QtArch

if ($LASTEXITCODE -ne 0) {
    throw "Qt installation failed."
}

Write-Host ""

if (Test-Path "$QtPath\bin\qt-cmake.bat") {
    Write-Host "Qt installation finished." -ForegroundColor Green
    Write-Host "Qt found at: $QtPath" -ForegroundColor Green
    Write-Host "qt-cmake: $QtPath\bin\qt-cmake.bat" -ForegroundColor Green
}
else {
    throw "Qt installation directory was not found: $QtPath"
}