# Compila GLSL -> SPIR-V e embute no binário (sem precisar carregar assets em runtime).
# Usa o glslc do NDK no Android; no desktop, glslc ou glslangValidator do Vulkan SDK.
set(_vc_hints "")
if(ANDROID_NDK)
  file(GLOB _vc_hints "${ANDROID_NDK}/shader-tools/*")
endif()
find_program(VC_GLSLC glslc HINTS ${_vc_hints} $ENV{VULKAN_SDK}/bin)
if(NOT VC_GLSLC)
  find_program(VC_GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/bin)
  if(NOT VC_GLSLANG)
    message(FATAL_ERROR "Nem glslc nem glslangValidator encontrados (instale o Vulkan SDK / shaderc).")
  endif()
endif()

function(vc_add_shaders target out_dir)
  set(outputs "")
  foreach(src ${ARGN})
    get_filename_component(abs "${src}" ABSOLUTE)
    get_filename_component(name "${src}" NAME)
    set(spv "${out_dir}/${name}.spv")
    set(inc "${out_dir}/${name}.inc")
    if(VC_GLSLC)
      set(compile_cmd "${VC_GLSLC}" -O --target-env=vulkan1.0 -o "${spv}" "${abs}")
    else()
      set(compile_cmd "${VC_GLSLANG}" -V --target-env vulkan1.0 -o "${spv}" "${abs}")
    endif()
    add_custom_command(
      OUTPUT "${inc}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
      COMMAND ${compile_cmd}
      COMMAND ${CMAKE_COMMAND} -DIN=${spv} -DOUT=${inc} -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedSpirv.cmake"
      DEPENDS "${abs}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedSpirv.cmake"
      COMMENT "Shader ${name}"
      VERBATIM)
    list(APPEND outputs "${inc}")
  endforeach()
  add_custom_target(${target} DEPENDS ${outputs})
endfunction()
