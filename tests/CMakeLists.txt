# Firedrake/tests/CMakeLists.txt
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
# See LICENSE.txt in the top level directory for licensing details
#
# Test kernels (vadd: m_axi + ap_ctrl_chain; regfile: s_axilite-only
# ap_ctrl_none), the firedraketest host element and the ctest suite.
# Each test runs tests/sdl/firedrake_test.py and passes on result=PASS.
#
# Labels: csim (always), sw_emu / hw_emu / hw (FIREDRAKE_TARGETS with XRT
# and FIREDRAKE_PLATFORM).

# ---------------------------------------------------
# kernels
# ---------------------------------------------------
set(KDIR ${CMAKE_CURRENT_SOURCE_DIR}/kernels)

firedrake_generate_component(NAME vadd
  METADATA ${KDIR}/vadd/kernel.xml
  OVERRIDES ${KDIR}/vadd/vadd.firedrake.json
  HLS_SOURCES ${KDIR}/vadd/vadd.cpp)

firedrake_generate_component(NAME regfile
  METADATA ${KDIR}/regfile/kernel.xml
  OVERRIDES ${KDIR}/regfile/regfile.firedrake.json
  HLS_SOURCES ${KDIR}/regfile/regfile.cpp)

# ---------------------------------------------------
# test host element
# ---------------------------------------------------
add_library(firedraketest SHARED elements/FiredrakeHost.cc)
target_include_directories(firedraketest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/firedrakevadd
  ${CMAKE_CURRENT_BINARY_DIR}/firedrakeregfile)
target_link_libraries(firedraketest PRIVATE firedrakecore)
# uses the generated headers only (typed accessors), not the kernel libraries
add_dependencies(firedraketest firedrakevadd firedrakeregfile)
firedrake_element_library(firedraketest)

# ---------------------------------------------------
# tests
# ---------------------------------------------------
set(FD_TEST_OUT "${CMAKE_CURRENT_BINARY_DIR}/out")
file(MAKE_DIRECTORY "${FD_TEST_OUT}")
set(FD_SDL "${CMAKE_CURRENT_SOURCE_DIR}/sdl/firedrake_test.py")

# firedrake_add_test(name label [TIMEOUT s] ARGS sdl-args...)
function(firedrake_add_test name label)
  cmake_parse_arguments(T "" "TIMEOUT" "ARGS" ${ARGN})
  if(NOT T_TIMEOUT)
    set(T_TIMEOUT 120)
  endif()
  add_test(NAME ${name}
           COMMAND ${SST} --add-lib-path=${FIREDRAKE_LIB_DIR} ${FD_SDL} -- ${T_ARGS}
           WORKING_DIRECTORY "${FD_TEST_OUT}")
  set_tests_properties(${name} PROPERTIES
    PASS_REGULAR_EXPRESSION "result=PASS"
    FAIL_REGULAR_EXPRESSION "result=FAIL|FATAL"
    TIMEOUT ${T_TIMEOUT}
    LABELS "${label}")
endfunction()

firedrake_add_test(vadd_direct_csim       csim ARGS --scenario vadd --mode direct)
firedrake_add_test(vadd_direct_runs_csim  csim ARGS --scenario vadd --mode direct --runs 3 --elements 1000)
firedrake_add_test(vadd_links_csim        csim ARGS --scenario vadd --mode links)
firedrake_add_test(vadd_links_runs_csim   csim ARGS --scenario vadd --mode links --runs 2 --elements 64)
firedrake_add_test(regfile_direct_csim    csim ARGS --scenario regfile --mode direct)

# XRT targets: build the xclbins with v++ and rerun the same scenarios
if(FIREDRAKE_TARGETS)
  if(NOT FIREDRAKE_HAVE_XRT OR NOT FIREDRAKE_PLATFORM)
    message(FATAL_ERROR "FIREDRAKE_TARGETS requires XRT and FIREDRAKE_PLATFORM")
  endif()
  foreach(target IN LISTS FIREDRAKE_TARGETS)
    set(timeout 600)
    if(target STREQUAL "hw_emu")
      set(timeout 3600)
    endif()
    foreach(k vadd regfile)
      firedrake_add_vitis_kernel(NAME ${k} SOURCES ${KDIR}/${k}/${k}.cpp TARGET ${target} PLATFORM ${FIREDRAKE_PLATFORM})
    endforeach()
    set(xrt_args --backend ${target})
    firedrake_add_test(vadd_direct_${target} ${target} TIMEOUT ${timeout}
      ARGS --scenario vadd --mode direct ${xrt_args} --xclbin ${vadd_XCLBIN} --emconfig ${vadd_EMCONFIG_DIR})
    firedrake_add_test(vadd_links_${target} ${target} TIMEOUT ${timeout}
      ARGS --scenario vadd --mode links ${xrt_args} --xclbin ${vadd_XCLBIN} --emconfig ${vadd_EMCONFIG_DIR})
    firedrake_add_test(regfile_direct_${target} ${target} TIMEOUT ${timeout}
      ARGS --scenario regfile --mode direct ${xrt_args} --xclbin ${regfile_XCLBIN} --emconfig ${regfile_EMCONFIG_DIR})
  endforeach()
endif()

# EOF
