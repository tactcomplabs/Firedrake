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
# and FIREDRAKE_PLATFORM), u50 (FIREDRAKE_U50_TEST with XRT and a U50).

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

# Alveo U50 hardware: build vadd into a U50 xclbin and run it on the card.
# The bitstream build is a ctest fixture (hours), never part of make:
#   ctest -L u50 --output-on-failure
if(FIREDRAKE_U50_TEST)
  if(NOT FIREDRAKE_HAVE_XRT)
    message(FATAL_ERROR "FIREDRAKE_U50_TEST requires XRT; source /opt/xilinx/xrt/setup.sh")
  endif()
  firedrake_add_vitis_kernel(NAME vadd SOURCES ${KDIR}/vadd/vadd.cpp TARGET hw
    PLATFORM ${FIREDRAKE_U50_PLATFORM} CONFIG ${KDIR}/vadd/vadd.u50.cfg TAG u50 EXCLUDE_FROM_ALL)

  add_test(NAME vadd_u50_device
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-alveo.sh ${FIREDRAKE_U50_PLATFORM} u50)
  set_tests_properties(vadd_u50_device PROPERTIES
    FIXTURES_SETUP u50_device TIMEOUT 60 LABELS u50)

  add_test(NAME vadd_u50_xclbin
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${vadd_XCLBIN_TARGET})
  set_tests_properties(vadd_u50_xclbin PROPERTIES
    FIXTURES_SETUP u50_xclbin TIMEOUT 14400 LABELS u50)

  set(u50_args --backend hw --xclbin ${vadd_XCLBIN})
  firedrake_add_test(vadd_direct_u50      u50 TIMEOUT 300 ARGS --scenario vadd --mode direct --elements 4096 ${u50_args})
  firedrake_add_test(vadd_direct_runs_u50 u50 TIMEOUT 300 ARGS --scenario vadd --mode direct --runs 3 --elements 1000 ${u50_args})
  firedrake_add_test(vadd_links_u50       u50 TIMEOUT 300 ARGS --scenario vadd --mode links --elements 4096 ${u50_args})
  set_tests_properties(vadd_direct_u50 vadd_direct_runs_u50 vadd_links_u50 PROPERTIES
    FIXTURES_REQUIRED "u50_device;u50_xclbin" RUN_SERIAL ON)
endif()

# EOF
