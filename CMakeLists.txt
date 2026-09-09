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
# Platform specific UI
# ---------------------------------------------------------------------------
if(WIN32)
    # Native Win32 UI.
    add_executable(CossacksLogViewer WIN32
        src/windows/CossacksLogViewer.cpp
        src/windows/CossacksLogViewer.h
        src/windows/CossacksLogViewer.rc
        src/windows/resource.h
        src/windows/framework.h
        src/windows/targetver.h
    )

    target_include_directories(CossacksLogViewer
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/windows
    )

    target_compile_definitions(CossacksLogViewer PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

    target_link_libraries(CossacksLogViewer
        PRIVATE
            clv_core
            comctl32
            ole32
            uxtheme
            shell32
    )

    if(MSVC)
        target_compile_options(CossacksLogViewer PRIVATE /W3)
    endif()
else()
    # Linux / other platforms: Qt Widgets UI.
    find_package(Qt6 QUIET COMPONENTS Widgets)

    if(Qt6_FOUND)
        set(CLV_QT_TARGET Qt6::Widgets)
    else()
        find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
        set(CLV_QT_TARGET Qt5::Widgets)
    endif()

    add_executable(CossacksLogViewer
        src/linux/main.cpp
        src/linux/MainWindow.cpp
        src/linux/MainWindow.h
    )

    target_include_directories(CossacksLogViewer
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(CossacksLogViewer
        PRIVATE
            clv_core
            ${CLV_QT_TARGET}
    )

    set_target_properties(CossacksLogViewer PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
    )
endif()
