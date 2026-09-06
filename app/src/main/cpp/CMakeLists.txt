# =============================================================================
#  Builds the native half of the educational overlay demo:
#    * our sources:   native-lib / overlay / demo_game (+ headers: memory,
#                     il2cpp_demo)
#    * ImGui core     : vendored under cpp/imgui (v1.90.4, MIT — see LICENSE)
#    * GLES3 backend  : vendored under cpp/imgui_impl
# =============================================================================
cmake_minimum_required(VERSION 3.22.1)
project(debugoverlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(native-lib SHARED
        native-lib.cpp
        overlay.cpp
        demo_game.cpp

        # ImGui core (ocornut/imgui v1.90.4)
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp

        # OpenGL ES 3 backend
        imgui_impl/imgui_impl_opengl3.cpp
        )

# cpp/          — our headers + sources (memory.h, il2cpp_demo.h, …)
# cpp/imgui     — so the backends' `#include "imgui.h"` resolves
target_include_directories(native-lib PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        )

# ImGui warnings aren't our problem; keep the log readable.
if (MSVC)
    target_compile_options(native-lib PRIVATE /W3)
else()
    target_compile_options(native-lib PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(native-lib
        EGL          # surface/context management
        GLESv3       # OpenGL ES 3.0 that ImGui renders through
        ${android-lib}
        ${log-lib}
        )
