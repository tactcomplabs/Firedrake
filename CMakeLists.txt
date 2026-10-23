# Firedrake Top-Level CMake
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
# See LICENSE.txt in the top level directory for licensing details
#
# Firedrake wraps Xilinx Vitis kernels (host side via XRT) in SST components.
#
# Options:
#   FIREDRAKE_ENABLE_TESTING   build the test kernels and register the ctest suite (ON)
#   FIREDRAKE_ENABLE_XRT       build the XRT backends (AUTO: ON when XRT is found)
#   FIREDRAKE_PLATFORM         Alveo platform (.xpfm or name) for v++ builds of the tests
#   FIREDRAKE_TARGETS          XRT targets to build/test: any of sw_emu;hw_emu;hw
#   FIREDRAKE_HLS_INCLUDE      Vitis HLS include dir (default: $XILINX_HLS/include)
#
# Retarget to your own kernel without editing this tree:
#   -DFIREDRAKE_KERNEL_NAME=<kernel> -DFIREDRAKE_KERNEL_METADATA=<kernel.xml|.xo|.xclbin|.json>
#   [-DFIREDRAKE_HLS_SOURCES="a.cpp;b.cpp"] [-DFIREDRAKE_KERNEL_OVERRIDES=<overrides.json>]

# ---------------------------------------------------
# string_contains_number
# ---------------------------------------------------
function(string_contains_number str result)
  string(REGEX MATCH "[0-9]" has_number "${str}")
  if(has_number)
    set(${result} TRUE PARENT_SCOPE)
  else()
    set(${result} FALSE PARENT_SCOPE)
  endif()
endfunction()

# ---------------------------------------------------
# main
# ---------------------------------------------------
# Prevent in-source builds
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message(FATAL_ERROR "DO NOT BUILD in-tree.")
endif()

cmake_minimum_required(VERSION 3.19)
project(firedrake CXX)

option(FIREDRAKE_ENABLE_TESTING "Enable Firedrake tests" ON)
set(FIREDRAKE_ENABLE_XRT "AUTO" CACHE STRING "Build the XRT backends (ON, OFF or AUTO)")
set(FIREDRAKE_PLATFORM "" CACHE STRING "Alveo platform used to build the test xclbins")
set(FIREDRAKE_TARGETS "" CACHE STRING "XRT targets to build and test (sw_emu;hw_emu;hw)")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()
set(FIREDRAKE_INSTALL_LIBDIR "lib/sst-elements")
set(FIREDRAKE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(FIREDRAKE_LIB_DIR "${CMAKE_BINARY_DIR}/lib")

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

# SST Configuration Sanity Check
find_program(SST sst)
find_program(SST_CONFIG sst-config)
if(NOT SST OR NOT SST_CONFIG)
  message(FATAL_ERROR "No SST binary or sst-config binary found in path")
endif()

# SST Environment Derivation
execute_process(COMMAND ${SST_CONFIG} --prefix
                OUTPUT_VARIABLE SST_INSTALL_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE)
set(SST_INCLUDE_DIR "${SST_INSTALL_DIR}/include")
if(NOT (EXISTS "${SST_INSTALL_DIR}"))
  message(FATAL_ERROR " SST_INSTALL_DIR (${SST_INSTALL_DIR}) is invalid.")
endif()

execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/sst-major-version.sh
                OUTPUT_VARIABLE SST_MAJOR_VERSION
                OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/sst-minor-version.sh
                OUTPUT_VARIABLE SST_MINOR_VERSION
                OUTPUT_STRIP_TRAILING_WHITESPACE)

string_contains_number("${SST_MAJOR_VERSION}" contains_num)
if(NOT contains_num)
  set(SST_MAJOR_VERSION "DEV")
  set(SST_MINOR_VERSION "DEV")
endif()

string(CONCAT SST_VERSION ${SST_MAJOR_VERSION} "." ${SST_MINOR_VERSION})
message(STATUS "SST FULL VERSION=${SST_VERSION}")

if(NOT "${SST_MAJOR_VERSION}" STREQUAL "DEV")
  if(${SST_VERSION} VERSION_LESS "16.0")
    message(FATAL_ERROR "Firedrake requires SST 16.0.0+")
  endif()
endif()

# ---------------------------------------------------
# Xilinx tools
#   XRT:  $XILINX_XRT (source /opt/xilinx/xrt/setup.sh)
#   HLS:  $XILINX_HLS/include, for the csim backend
# ---------------------------------------------------
set(FIREDRAKE_HAVE_XRT OFF)
if(NOT FIREDRAKE_ENABLE_XRT STREQUAL "OFF")
  find_package(XRT CONFIG QUIET PATHS "$ENV{XILINX_XRT}/share/cmake/XRT" "/opt/xilinx/xrt/share/cmake/XRT")
  if(XRT_FOUND)
    set(FIREDRAKE_HAVE_XRT ON)
    message(STATUS "XRT: ${XRT_DIR}")
  elseif(FIREDRAKE_ENABLE_XRT STREQUAL "ON")
    message(FATAL_ERROR "FIREDRAKE_ENABLE_XRT=ON but XRT was not found; source /opt/xilinx/xrt/setup.sh")
  else()
    message(STATUS "XRT not found: only the csim backend will be built")
  endif()
endif()

if(NOT FIREDRAKE_HLS_INCLUDE)
  foreach(hls "$ENV{XILINX_HLS}" "$ENV{XILINX_VITIS}")
    if(hls AND EXISTS "${hls}/include/hls_stream.h")
      set(FIREDRAKE_HLS_INCLUDE "${hls}/include")
      break()
    endif()
  endforeach()
endif()
set(FIREDRAKE_HLS_INCLUDE "${FIREDRAKE_HLS_INCLUDE}" CACHE PATH "Vitis HLS include directory")
message(STATUS "Vitis HLS headers: ${FIREDRAKE_HLS_INCLUDE}")

# Compiler Options
if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wmissing-format-attribute -Wsuggest-final-methods -Wsuggest-final-types")
endif()

set(CMAKE_CXX_FLAGS "-std=c++17 -O2 -fPIC -Wall -Wextra -Wsuggest-override -Wmissing-noreturn -Wvla -Wuninitialized -Wdouble-promotion -Wsign-conversion -Wno-unused-parameter -Wno-deprecated-declarations -Werror ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Firedrake.cmake)

# Subdirectories
add_subdirectory(src)

if(FIREDRAKE_KERNEL_NAME)
  firedrake_generate_component(NAME ${FIREDRAKE_KERNEL_NAME}
                               METADATA ${FIREDRAKE_KERNEL_METADATA}
                               HLS_SOURCES ${FIREDRAKE_HLS_SOURCES}
                               OVERRIDES ${FIREDRAKE_KERNEL_OVERRIDES})
endif()

if(FIREDRAKE_ENABLE_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

# EOF
