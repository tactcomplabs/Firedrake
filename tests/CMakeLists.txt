# Firedrake/tests/CMakeLists.txt
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
# See LICENSE.txt in the top level directory for licensing details
#
# Test kernels (vadd: m_axi + ap_ctrl_chain; regfile: s_axilite-only
# ap_ctrl_none; picorv32_kernel: an RTL kernel wrapping the PicoRV32 core),
# the firedraketest host elements and the ctest suite.  Each test runs
# tests/sdl/firedrake_test.py and passes on result=PASS.
#
# Labels: csim (always), sw_emu / hw_emu / hw (FIREDRAKE_TARGETS with XRT
# and FIREDRAKE_PLATFORM), u50 (FIREDRAKE_U50_TEST with XRT and a U50;
# the picorv32 tests also carry the label picorv32).

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

# RTL kernel: no HLS sources, so only the XRT backends can run it
firedrake_generate_component(NAME picorv32_kernel
  METADATA ${KDIR}/picorv32/kernel.xml
  OVERRIDES ${KDIR}/picorv32/picorv32_kernel.firedrake.json)

# ---------------------------------------------------
# test host elements
# ---------------------------------------------------
add_library(firedraketest SHARED elements/FiredrakeHost.cc elements/PicoRV32Host.cc)
target_include_directories(firedraketest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/firedrakevadd
  ${CMAKE_CURRENT_BINARY_DIR}/firedrakeregfile
  ${CMAKE_CURRENT_BINARY_DIR}/firedrakepicorv32_kernel
  ${KDIR}/picorv32/firmware)
target_link_libraries(firedraketest PRIVATE firedrakecore)
# uses the generated headers only (typed accessors), not the kernel libraries
add_dependencies(firedraketest firedrakevadd firedrakeregfile firedrakepicorv32_kernel)
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

  # picorv32: the PicoRV32 core as an RTL kernel running RISC-V firmware.
  # make builds the firmware; ctest simulates the RTL with xsim, packages
  # the .xo with Vivado and links the U50 bitstream (hours), then runs it.
  set(PDIR ${KDIR}/picorv32)
  find_program(VIVADO vivado)
  find_program(RISCV_GCC NAMES riscv64-unknown-elf-gcc riscv32-unknown-elf-gcc
               HINTS "$ENV{XILINX_VITIS}/gnu/riscv/lin/bin")
  if(NOT VIVADO OR NOT RISCV_GCC)
    message(FATAL_ERROR "FIREDRAKE_U50_TEST: the picorv32 test needs vivado and a RISC-V gcc "
                        "(source the Vitis settings, or set VIVADO / RISCV_GCC)")
  endif()
  string(REGEX REPLACE "gcc$" "objcopy" RISCV_OBJCOPY "${RISCV_GCC}")

  set(fw_dir "${CMAKE_CURRENT_BINARY_DIR}/picorv32_firmware")
  set(fw_srcs ${PDIR}/firmware/start.S ${PDIR}/firmware/firmware.c)
  set(fw_deps ${fw_srcs} ${PDIR}/firmware/firmware.ld ${PDIR}/firmware/picorv32_test.h)
  file(MAKE_DIRECTORY "${fw_dir}")
  add_custom_command(
    OUTPUT "${fw_dir}/firmware.bin" "${fw_dir}/firmware.hex"
    COMMAND ${RISCV_GCC} -march=rv32im -mabi=ilp32 -O2 -ffreestanding -nostdlib -fno-builtin
            -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror -I${PDIR}/firmware
            -T ${PDIR}/firmware/firmware.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments
            -o "${fw_dir}/firmware.elf" ${fw_srcs} -lgcc
    COMMAND ${RISCV_OBJCOPY} -O binary "${fw_dir}/firmware.elf" "${fw_dir}/firmware.bin"
    COMMAND ${Python3_EXECUTABLE} ${PDIR}/firmware/bin2hex.py "${fw_dir}/firmware.bin" "${fw_dir}/firmware.hex"
    DEPENDS ${fw_deps} ${PDIR}/firmware/bin2hex.py
    COMMENT "Building the PicoRV32 test firmware"
    VERBATIM)
  add_custom_target(picorv32_firmware ALL DEPENDS "${fw_dir}/firmware.bin" "${fw_dir}/firmware.hex")

  set(pico_rtl ${PDIR}/picorv32.v ${PDIR}/picorv32_kernel_control.v ${PDIR}/picorv32_kernel.v)
  set(pico_xo "${CMAKE_CURRENT_BINARY_DIR}/picorv32_kernel.xo/picorv32_kernel.xo")
  add_custom_command(
    OUTPUT "${pico_xo}"
    COMMAND ${VIVADO} -mode batch -nojournal -nolog -source ${PDIR}/package_kernel.tcl
            -tclargs "${pico_xo}" "${CMAKE_CURRENT_BINARY_DIR}/picorv32_kernel.xo"
    DEPENDS ${pico_rtl} ${PDIR}/kernel.xml ${PDIR}/package_kernel.tcl
    COMMENT "Packaging the picorv32_kernel RTL kernel"
    VERBATIM)
  firedrake_add_vitis_kernel(NAME picorv32_kernel XO "${pico_xo}" TARGET hw
    PLATFORM ${FIREDRAKE_U50_PLATFORM} CONFIG ${PDIR}/picorv32.u50.cfg TAG u50 EXCLUDE_FROM_ALL)

  # RTL check before the bitstream build
  add_test(NAME picorv32_rtl_xsim
           COMMAND ${PDIR}/tb/run_xsim.sh "${fw_dir}/firmware.hex" "${CMAKE_CURRENT_BINARY_DIR}/picorv32_xsim")
  set_tests_properties(picorv32_rtl_xsim PROPERTIES
    PASS_REGULAR_EXPRESSION "result=PASS"
    FAIL_REGULAR_EXPRESSION "result=FAIL|ERROR"
    FIXTURES_SETUP picorv32_rtl TIMEOUT 900 LABELS "u50;picorv32")

  add_test(NAME picorv32_u50_xclbin
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${picorv32_kernel_XCLBIN_TARGET})
  set_tests_properties(picorv32_u50_xclbin PROPERTIES
    FIXTURES_SETUP picorv32_u50_xclbin FIXTURES_REQUIRED picorv32_rtl TIMEOUT 14400 LABELS "u50;picorv32")

  set(pico_args --scenario picorv32 --backend hw --xclbin ${picorv32_kernel_XCLBIN} --program ${fw_dir}/firmware.bin)
  firedrake_add_test(picorv32_direct_u50      "u50;picorv32" TIMEOUT 300 ARGS ${pico_args} --mode direct --elements 256 --timeout-cycles 500)
  firedrake_add_test(picorv32_direct_runs_u50 "u50;picorv32" TIMEOUT 300 ARGS ${pico_args} --mode direct --elements 1024 --runs 3)
  firedrake_add_test(picorv32_links_u50       "u50;picorv32" TIMEOUT 300 ARGS ${pico_args} --mode links --elements 128 --runs 2)
  set_tests_properties(picorv32_direct_u50 picorv32_direct_runs_u50 picorv32_links_u50 PROPERTIES
    FIXTURES_REQUIRED "u50_device;picorv32_u50_xclbin" RUN_SERIAL ON)
endif()

# EOF
