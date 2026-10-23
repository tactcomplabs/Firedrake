# Firedrake/cmake/Firedrake.cmake
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
# See LICENSE.txt in the top level directory for licensing details
#
# Retargeting entry points:
#
#   firedrake_generate_component(NAME <kernel> METADATA <kernel.xml|.xo|.xclbin|.json>
#                                [HLS_SOURCES src...] [HLS_INCLUDES dir...]
#                                [OVERRIDES <overrides.json>] [KERNEL <name>])
#     Generates and builds the SST element library firedrake<kernel> (one
#     SubComponent, Firedrake<Kernel>).  HLS_SOURCES are compiled natively
#     for the csim backend; without them only the XRT backends are usable.
#
#   firedrake_add_vitis_kernel(NAME <kernel> SOURCES src... TARGET sw_emu|hw_emu|hw
#                              PLATFORM <xpfm> [CONFIG <v++ cfg>] [CLOCK <Hz>])
#     Builds <kernel>.<target>.xo / .xclbin (and emconfig.json for
#     emulation) with v++ and sets <kernel>_XCLBIN / <kernel>_XO in the
#     parent scope.
#
# Requires FIREDRAKE_SOURCE_DIR, FIREDRAKE_LIB_DIR, Python3 and the
# firedrakecore target (all provided by the top-level CMakeLists.txt).

set(FIREDRAKE_GEN "${FIREDRAKE_SOURCE_DIR}/scripts/firedrake-gen.py")
set(FIREDRAKE_TEMPLATES
  "${FIREDRAKE_SOURCE_DIR}/templates/Subcomponent.h.in"
  "${FIREDRAKE_SOURCE_DIR}/templates/Subcomponent.cc.in")

# Converts a kernel name to its generated class suffix (vadd -> Vadd, my_k -> MyK)
function(_firedrake_class_suffix name out)
  string(REPLACE "_" ";" parts "${name}")
  set(result "")
  foreach(p IN LISTS parts)
    if(p STREQUAL "")
      continue()
    endif()
    string(SUBSTRING "${p}" 0 1 first)
    string(SUBSTRING "${p}" 1 -1 rest)
    string(TOUPPER "${first}" first)
    string(APPEND result "${first}${rest}")
  endforeach()
  set(${out} "${result}" PARENT_SCOPE)
endfunction()

function(firedrake_generate_component)
  cmake_parse_arguments(F "" "NAME;METADATA;OVERRIDES;KERNEL" "HLS_SOURCES;HLS_INCLUDES" ${ARGN})
  if(NOT F_NAME OR NOT F_METADATA)
    message(FATAL_ERROR "firedrake_generate_component: NAME and METADATA are required")
  endif()
  get_filename_component(F_METADATA "${F_METADATA}" ABSOLUTE)

  _firedrake_class_suffix("${F_NAME}" suffix)
  set(cls "Firedrake${suffix}")
  string(TOLOWER "firedrake${F_NAME}" lib)
  set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/${lib}")
  set(gen_src "${gen_dir}/${cls}.h" "${gen_dir}/${cls}.cc" "${gen_dir}/${F_NAME}.json")

  set(gen_args --metadata "${F_METADATA}" --out "${gen_dir}" --name "${F_NAME}")
  set(gen_deps "${F_METADATA}" "${FIREDRAKE_GEN}" ${FIREDRAKE_TEMPLATES})
  if(F_KERNEL)
    list(APPEND gen_args --kernel "${F_KERNEL}")
  endif()
  if(F_OVERRIDES)
    get_filename_component(F_OVERRIDES "${F_OVERRIDES}" ABSOLUTE)
    list(APPEND gen_args --overrides "${F_OVERRIDES}")
    list(APPEND gen_deps "${F_OVERRIDES}")
  endif()
  if(F_HLS_SOURCES)
    list(APPEND gen_args --csim)
  endif()

  add_custom_command(
    OUTPUT ${gen_src}
    COMMAND ${Python3_EXECUTABLE} ${FIREDRAKE_GEN} ${gen_args}
    DEPENDS ${gen_deps}
    COMMENT "Generating ${cls} from ${F_METADATA}"
    VERBATIM)

  add_library(${lib} SHARED "${gen_dir}/${cls}.cc" "${gen_dir}/${cls}.h")
  target_include_directories(${lib} PUBLIC "${gen_dir}")
  target_link_libraries(${lib} PRIVATE firedrakecore)

  if(F_HLS_SOURCES)
    if(NOT FIREDRAKE_HLS_INCLUDE)
      message(FATAL_ERROR "firedrake_generate_component(${F_NAME}): HLS_SOURCES need the Vitis HLS headers; "
                          "set XILINX_HLS or FIREDRAKE_HLS_INCLUDE")
    endif()
    # HLS kernel code is compiled as-is: the project warning set does not apply
    add_library(${lib}_csim OBJECT ${F_HLS_SOURCES})
    target_include_directories(${lib}_csim SYSTEM PRIVATE ${FIREDRAKE_HLS_INCLUDE} ${F_HLS_INCLUDES})
    set_target_properties(${lib}_csim PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(${lib}_csim PRIVATE -w -Wno-error)
    target_sources(${lib} PRIVATE $<TARGET_OBJECTS:${lib}_csim>)
  endif()

  firedrake_element_library(${lib})
endfunction()

# Common output, rpath, install and sst-register handling for element libraries
function(firedrake_element_library lib)
  set_target_properties(${lib} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${FIREDRAKE_LIB_DIR}"
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN")
  install(TARGETS ${lib} LIBRARY DESTINATION ${FIREDRAKE_INSTALL_LIBDIR})
  install(CODE "execute_process(COMMAND ${SST}-register ${lib} ${lib}_LIBDIR=${CMAKE_INSTALL_PREFIX}/${FIREDRAKE_INSTALL_LIBDIR})")
endfunction()

function(firedrake_add_vitis_kernel)
  cmake_parse_arguments(F "" "NAME;TARGET;PLATFORM;CONFIG;CLOCK" "SOURCES" ${ARGN})
  if(NOT F_NAME OR NOT F_SOURCES OR NOT F_TARGET OR NOT F_PLATFORM)
    message(FATAL_ERROR "firedrake_add_vitis_kernel: NAME, SOURCES, TARGET and PLATFORM are required")
  endif()
  find_program(VPP v++ REQUIRED)
  find_program(EMCONFIGUTIL emconfigutil)
  if(NOT F_CLOCK)
    set(F_CLOCK 300000000)
  endif()

  set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/${F_NAME}.${F_TARGET}")
  set(xo "${out_dir}/${F_NAME}.xo")
  set(xclbin "${out_dir}/${F_NAME}.xclbin")
  set(cfg_args)
  if(F_CONFIG)
    get_filename_component(F_CONFIG "${F_CONFIG}" ABSOLUTE)
    set(cfg_args --config "${F_CONFIG}")
  endif()
  set(srcs)
  foreach(s IN LISTS F_SOURCES)
    get_filename_component(s "${s}" ABSOLUTE)
    list(APPEND srcs "${s}")
  endforeach()

  add_custom_command(
    OUTPUT "${xo}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
    COMMAND ${VPP} -c -t ${F_TARGET} --platform ${F_PLATFORM} -k ${F_NAME} --hls.clock ${F_CLOCK}:${F_NAME}
            --temp_dir "${out_dir}/_x" ${cfg_args} -o "${xo}" ${srcs}
    DEPENDS ${srcs} ${F_CONFIG}
    WORKING_DIRECTORY "${out_dir}"
    COMMENT "v++ -c ${F_NAME} (${F_TARGET})"
    VERBATIM)
  add_custom_command(
    OUTPUT "${xclbin}"
    COMMAND ${VPP} -l -t ${F_TARGET} --platform ${F_PLATFORM} --temp_dir "${out_dir}/_x" ${cfg_args}
            -o "${xclbin}" "${xo}"
    DEPENDS "${xo}" ${F_CONFIG}
    WORKING_DIRECTORY "${out_dir}"
    COMMENT "v++ -l ${F_NAME} (${F_TARGET})"
    VERBATIM)
  set(outputs "${xclbin}")
  if(NOT F_TARGET STREQUAL "hw")
    if(NOT EMCONFIGUTIL)
      message(FATAL_ERROR "firedrake_add_vitis_kernel: emconfigutil is required for ${F_TARGET}")
    endif()
    add_custom_command(
      OUTPUT "${out_dir}/emconfig.json"
      COMMAND ${EMCONFIGUTIL} --platform ${F_PLATFORM} --od "${out_dir}"
      WORKING_DIRECTORY "${out_dir}"
      COMMENT "emconfigutil (${F_TARGET})"
      VERBATIM)
    list(APPEND outputs "${out_dir}/emconfig.json")
  endif()
  add_custom_target(${F_NAME}_${F_TARGET}_xclbin ALL DEPENDS ${outputs})

  set(${F_NAME}_XO "${xo}" PARENT_SCOPE)
  set(${F_NAME}_XCLBIN "${xclbin}" PARENT_SCOPE)
  set(${F_NAME}_EMCONFIG_DIR "${out_dir}" PARENT_SCOPE)
endfunction()

# EOF
