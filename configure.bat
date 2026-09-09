@echo off
rem ============================================================================
rem  configure.bat - Generate the CMake build tree.
rem
rem  Usage:
rem    configure.bat                 Generates into .\build
rem    configure.bat -A Win32        ...or pass any extra CMake options
rem    configure.bat -DCMAKE_BUILD_TYPE=Release
rem ============================================================================
setlocal
set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"

cmake -S "%ROOT%." -B "%BUILD_DIR%" %*

exit /b %errorlevel%
