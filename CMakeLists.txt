cmake_minimum_required(VERSION 3.21)

project(CossacksLogViewer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Core library (platform independent)
# ---------------------------------------------------------------------------
add_library(clv_core STATIC
    src/core/TextUtils.cpp
    src/core/LogModel.cpp
    src/core/LogParser.cpp
    src/core/GameDirectory.cpp
)

target_include_directories(clv_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(clv_core PRIVATE /W3)
else()
    target_compile_options(clv_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# UI (Qt Widgets - common cross-platform UI)
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Widgets)

if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 QUIET COMPONENTS Widgets)
endif()

if(NOT Qt6_FOUND AND NOT Qt5_FOUND)
    message(FATAL_ERROR
        "Qt Widgets (Qt 5.15+ or Qt 6) was not found.\n"
        "Point CMake at your Qt kit and re-run configure, for example:\n"
        "  configure.bat -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/<kit>\n"
        "  ./configure.sh -DCMAKE_PREFIX_PATH=/opt/Qt/6.11.2/<kit>")
endif()

if(Qt6_FOUND)
    set(CLV_QT_TARGET Qt6::Widgets)
    set(CLV_QT_MAJOR 6)
else()
    set(CLV_QT_TARGET Qt5::Widgets)
    set(CLV_QT_MAJOR 5)
endif()

set(CLV_UI_SOURCES
    src/ui/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
)

if(WIN32)
    set(CLV_WIN32_EXE WIN32)
else()
    set(CLV_WIN32_EXE)
endif()

if(CLV_QT_MAJOR EQUAL 6)
    # qt_add_executable handles the Windows GUI entry point for Qt 6.
    if(COMMAND qt_add_executable)
        qt_add_executable(CossacksLogViewer ${CLV_WIN32_EXE} ${CLV_UI_SOURCES})
    else()
        add_executable(CossacksLogViewer ${CLV_WIN32_EXE} ${CLV_UI_SOURCES})
    endif()
else()
    add_executable(CossacksLogViewer ${CLV_WIN32_EXE} ${CLV_UI_SOURCES})
endif()

target_include_directories(CossacksLogViewer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(CossacksLogViewer
    PRIVATE
        clv_core
        ${CLV_QT_TARGET}
)

if(WIN32)
    # Registry access for Steam/GOG game detection.
    target_link_libraries(CossacksLogViewer PRIVATE advapi32)
    target_compile_definitions(CossacksLogViewer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    # Copy the required Qt DLLs and plugins next to the executable after
    # linking, so the app can run without the Qt "bin" directory on PATH.
    if(CLV_QT_MAJOR EQUAL 6)
        set(_clv_qmake_target Qt6::qmake)
    else()
        set(_clv_qmake_target Qt5::qmake)
    endif()

    if(TARGET ${_clv_qmake_target})
        get_target_property(_clv_qmake_location ${_clv_qmake_target} IMPORTED_LOCATION)
        get_filename_component(_clv_qt_bin_dir "${_clv_qmake_location}" DIRECTORY)

        find_program(CLV_WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt
            HINTS "${_clv_qt_bin_dir}"
        )
    endif()

    if(CLV_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET CossacksLogViewer POST_BUILD
            COMMAND "${CLV_WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-compiler-runtime
                    --skip-plugin-types=imageformats,iconengines
                    "$<TARGET_FILE:CossacksLogViewer>"
            VERBATIM
            COMMENT "Deploying Qt runtime dependencies (windeployqt)..."
        )
    else()
        message(WARNING
            "windeployqt was not found; the executable will need the Qt 'bin' "
            "directory on PATH in order to run.")
    endif()
endif()

if(MSVC)
    target_compile_options(CossacksLogViewer PRIVATE /W3)
endif()

set_target_properties(CossacksLogViewer PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
)
