@echo off
setlocal

set "ROOT=%~dp0"
set "SOLUTION=%ROOT%CossacksLogViewer.sln"
set "MSBUILD="

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=* delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\Current\Bin\MSBuild.exe`) do (
        set "MSBUILD=%%I"
    )
)

if not defined MSBUILD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
)

if not defined MSBUILD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
)

if not defined MSBUILD (
    echo MSBuild was not found. Install Visual Studio or Build Tools with C++ build tools.
    exit /b 1
)

echo Building production Release x86...
"%MSBUILD%" "%SOLUTION%" /p:Configuration=Release /p:Platform=x86 /m
if errorlevel 1 exit /b %errorlevel%

echo.
echo Build complete: "%ROOT%Release\CossacksLogViewer.exe"
