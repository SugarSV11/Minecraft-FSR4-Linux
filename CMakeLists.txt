cmake_minimum_required(VERSION 3.20)
project(minecraft_fsr4_linux LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
find_package(Threads REQUIRED)

# Vulkan headers only (the loader is resolved with dlopen at runtime, exactly like Minecraft's LWJGL does).
set(VULKAN_HEADERS_DIR "$ENV{HOME}/.cache/fsr4-linux/vk-headers" CACHE PATH "directory containing vulkan/vulkan.h")

add_library(fsr4linux SHARED native/src/fsr4_capi.cpp native/src/bridge_client.cpp native/src/gpu_bridge.cpp native/src/prep_pass.cpp native/src/fgx3_pass.cpp)
target_include_directories(fsr4linux PRIVATE native/include ${VULKAN_HEADERS_DIR})
target_link_libraries(fsr4linux PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(fsr4linux PROPERTIES OUTPUT_NAME "fsr4linux")

# Test programs (need the loader at link time).
find_library(VULKAN_LIB NAMES vulkan libvulkan.so.1 PATHS /usr/lib64 /usr/lib/x86_64-linux-gnu)
if(VULKAN_LIB)
  foreach(t ipc_test gpu_ipc_test prep_test fgx3_test crc_stress fgx3_replay)
    add_executable(${t} tests/${t}.cpp native/src/bridge_client.cpp native/src/gpu_bridge.cpp native/src/prep_pass.cpp native/src/fgx3_pass.cpp)
    target_include_directories(${t} PRIVATE native/include ${VULKAN_HEADERS_DIR})
    target_link_libraries(${t} PRIVATE Threads::Threads ${VULKAN_LIB})
  endforeach()
endif()
