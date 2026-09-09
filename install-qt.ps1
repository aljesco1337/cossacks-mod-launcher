$ErrorActionPreference = "Stop"

$QtVersion = "6.11.2"
$QtPackage = "qt.qt6.6112.win64_msvc2022_64"
$QtRoot = "C:\Qt"
$Installer = "$env:TEMP\qt-online-installer.exe"
$InstallerUrl = "https://download.qt.io/official_releases/online_installers/qt-online-installer-windows-x64-online.exe"

Write-Host "Downloading Qt Online Installer..." -ForegroundColor Cyan

Invoke-WebRequest `
    -Uri $InstallerUrl `
    -OutFile $Installer

Write-Host "Installer downloaded to: $Installer" -ForegroundColor Green

Write-Host "Installing Qt $QtVersion MSVC 2022 64-bit..." -ForegroundColor Cyan

& $Installer `
    --root $QtRoot `
    --accept-licenses `
    --accept-obligations `
    --confirm-command `
    install $QtPackage

Write-Host ""
Write-Host "Qt installation finished." -ForegroundColor Green

$QtPath = "$QtRoot\$QtVersion\msvc2022_64"

if (Test-Path "$QtPath\bin\qt-cmake.bat") {
    Write-Host "Qt found at: $QtPath" -ForegroundColor Green
    Write-Host "qt-cmake: $QtPath\bin\qt-cmake.bat" -ForegroundColor Green
}
else {
    Write-Warning "Qt installation directory was not found."
    Write-Warning "You may need to authenticate with your Qt Account."
}