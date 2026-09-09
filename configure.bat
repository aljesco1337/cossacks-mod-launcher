@echo off
rem ============================================================================
rem  configure.bat - Generate the CMake build tree.
rem
rem  If no Qt location is supplied, this script looks for a Qt installation
rem  under C:\Qt and forwards it to CMake via CMAKE_PREFIX_PATH.
rem
rem  Usage:
rem    configure.bat                                  Generates into .\build
rem    configure.bat -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64
rem    configure.bat ...                              any extra CMake options
rem ============================================================================
setlocal EnableDelayedExpansion
set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"

rem Detect whether the caller already supplied a Qt location hint.
set "HAS_QT_HINT="
for %%A in (%*) do (
    set "ARG=%%A"
    if not "!ARG:CMAKE_PREFIX_PATH=!"=="!ARG!" set "HAS_QT_HINT=1"
    if not "!ARG:Qt_DIR=!"=="!ARG!" set "HAS_QT_HINT=1"
    if not "!ARG:Qt5_DIR=!"=="!ARG!" set "HAS_QT_HINT=1"
    if not "!ARG:Qt6_DIR=!"=="!ARG!" set "HAS_QT_HINT=1"
)

set "QT_PREFIX="

if not defined HAS_QT_HINT (
    if exist "C:\Qt\" (
        rem Qt kits are usually installed as C:\Qt\<version>\<kit>. Prefer Qt6.
        for /d %%V in ("C:\Qt\*") do (
            for /d %%K in ("%%V\*") do (
                if exist "%%K\lib\cmake\Qt6\Qt6Config.cmake" if not defined QT_PREFIX set "QT_PREFIX=%%K"
            )
        )
        if not defined QT_PREFIX (
            for /d %%K in ("C:\Qt\*") do (
                if exist "%%K\lib\cmake\Qt6\Qt6Config.cmake" if not defined QT_PREFIX set "QT_PREFIX=%%K"
            )
        )
        if not defined QT_PREFIX (
            for /d %%V in ("C:\Qt\*") do (
                for /d %%K in ("%%V\*") do (
                    if exist "%%K\lib\cmake\Qt5\Qt5Config.cmake" if not defined QT_PREFIX set "QT_PREFIX=%%K"
                )
            )
        )
        if not defined QT_PREFIX (
            for /d %%K in ("C:\Qt\*") do (
                if exist "%%K\lib\cmake\Qt5\Qt5Config.cmake" if not defined QT_PREFIX set "QT_PREFIX=%%K"
            )
        )
    )
)

if defined QT_PREFIX (
    echo [configure] Qt detected at: !QT_PREFIX!
    cmake -S "%ROOT%." -B "%BUILD_DIR%" "-DCMAKE_PREFIX_PATH=!QT_PREFIX!" %*
) else (
    cmake -S "%ROOT%." -B "%BUILD_DIR%" %*
)

exit /b %errorlevel%
