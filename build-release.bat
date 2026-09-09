@echo off
rem ============================================================================
rem  build-release.bat - Build Release binaries (configures first if needed).
rem
rem  Usage:
rem    build-release.bat             Builds Release into .\build\Release
rem    build-release.bat --parallel  ...or pass any extra CMake build options
rem ============================================================================
setlocal
set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"

if not exist "%BUILD_DIR%CMakeCache.txt" (
    echo Configuring CMake build tree...
    call "%ROOT%configure.bat" || exit /b 1
)

cmake --build "%BUILD_DIR%" --config Release %*

exit /b %errorlevel%
