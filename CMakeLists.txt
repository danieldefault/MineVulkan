cmake_minimum_required(VERSION 3.22)
project(VoxelCraft LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- núcleo (sem GPU)
add_library(vc_core STATIC
  src/core/noise.cpp
  src/core/world.cpp
  src/core/mesher.cpp
  src/core/player.cpp
  src/core/texture_gen.cpp
  src/core/touch_controls.cpp)
target_include_directories(vc_core PUBLIC src)
target_compile_options(vc_core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# ---------------------------------------------------------------- shaders embutidos
include(cmake/Shaders.cmake)
set(VC_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
vc_add_shaders(vc_shaders "${VC_SHADER_DIR}"
  shaders/voxel.vert shaders/voxel.frag shaders/ui.vert shaders/ui.frag)

# ---------------------------------------------------------------- engine (Vulkan)
add_library(vc_engine STATIC src/render/renderer.cpp src/game/game.cpp)
add_dependencies(vc_engine vc_shaders)
target_include_directories(vc_engine PRIVATE "${VC_SHADER_DIR}")
target_compile_options(vc_engine PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

if(ANDROID)
  # Vulkan vem do NDK (headers + libvulkan.so do sistema).
  target_link_libraries(vc_engine PUBLIC vc_core vulkan)

  set(GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")
  add_library(native_app_glue STATIC "${GLUE_DIR}/android_native_app_glue.c")
  target_include_directories(native_app_glue PUBLIC "${GLUE_DIR}")

  add_library(voxelcraft SHARED src/platform/android/android_main.cpp)
  target_link_libraries(voxelcraft PRIVATE vc_engine native_app_glue android log)
  # ANativeActivity_onCreate é chamado pelo sistema, não pelo nosso código: força o linker a mantê-lo.
  target_link_options(voxelcraft PRIVATE "-u" "ANativeActivity_onCreate")
else()
  find_package(Vulkan REQUIRED)
  find_package(X11 REQUIRED)
  target_link_libraries(vc_engine PUBLIC vc_core Vulkan::Vulkan)

  add_executable(voxelcraft src/platform/desktop/desktop_main.cpp)
  if(NOT X11_xcb_FOUND)
    message(FATAL_ERROR "libxcb (headers) não encontrada")
  endif()
  target_include_directories(voxelcraft PRIVATE ${X11_xcb_INCLUDE_PATH})
  target_link_libraries(voxelcraft PRIVATE vc_engine ${X11_xcb_LIB})

  option(VC_BUILD_TESTS "Compilar testes do núcleo" ON)
  if(VC_BUILD_TESTS)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE vc_core)
  endif()
endif()
