# Alternative build with CMake. The main build is AirLabs.sln (Visual Studio), which is what
# bakkesplugins.com builds. Both use the SDK that BakkesMod installs in its own folder.
cmake_minimum_required(VERSION 3.20)
project(AirLabs LANGUAGES CXX)

# BakkesMod plugins are 64-bit Windows DLLs built with MSVC.
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Build with Visual Studio (MSVC), x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Override with -DBAKKESMOD_DIR=... if BakkesMod lives somewhere else.
set(BAKKESMOD_DIR "$ENV{APPDATA}/bakkesmod/bakkesmod" CACHE PATH "BakkesMod folder")
file(TO_CMAKE_PATH "${BAKKESMOD_DIR}" BAKKESMOD_DIR)
set(BM_SDK "${BAKKESMOD_DIR}/bakkesmodsdk")
if(NOT EXISTS "${BM_SDK}/include/bakkesmod/plugin/bakkesmodplugin.h")
  message(FATAL_ERROR "BakkesMod SDK not found at ${BM_SDK}. Install BakkesMod, or pass -DBAKKESMOD_DIR=...")
endif()

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/AirLabs")
add_library(AirLabs SHARED
  "${SRC}/AirLabs.cpp"
  "${SRC}/Leaderboard.cpp"
  "${SRC}/Replay.h"
  "${SRC}/AirLabs.h"
  "${SRC}/TrainerWindow.cpp"
  "${SRC}/TrainerCore.h"
  "${SRC}/BoostLab.h"
  "${SRC}/EmbeddedFonts.h"
  "${SRC}/imgui/imgui.cpp"
  "${SRC}/imgui/imgui_draw.cpp"
  "${SRC}/imgui/imgui_widgets.cpp")

target_include_directories(AirLabs PRIVATE "${BM_SDK}/include" "${SRC}")
target_link_libraries(AirLabs PRIVATE "${BM_SDK}/lib/pluginsdk.lib")
target_compile_definitions(AirLabs PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(AirLabs PRIVATE /MP /utf-8)

# Copy the DLL into BakkesMod after each build (the window font is inside the DLL).
if(EXISTS "${BAKKESMOD_DIR}/plugins")
  add_custom_command(TARGET AirLabs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:AirLabs>" "${BAKKESMOD_DIR}/plugins/AirLabs.dll"
    COMMENT "Copying AirLabs.dll to ${BAKKESMOD_DIR}/plugins")
endif()
