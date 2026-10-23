# Firedrake/src/CMakeLists.txt
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
# See LICENSE.txt in the top level directory for licensing details
#
# this is not meant to be standalone
# this is triggered via the top-level CMakeLists.txt in Firedrake
#
# firedrakecore: kernel-independent runtime (backends, latency model,
#                FiredrakeKernel) shared by every generated kernel library
# firedrake:     SST element library holding FiredrakeComponent

set(FiredrakeCoreSrcs
        Backend.cc
        CsimBackend.cc
        FiredrakeKernel.cc
        LatencyModel.cc
)
if(FIREDRAKE_HAVE_XRT)
  list(APPEND FiredrakeCoreSrcs XrtBackend.cc)
endif()

add_library(firedrakecore SHARED ${FiredrakeCoreSrcs})
target_include_directories(firedrakecore PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(firedrakecore SYSTEM PUBLIC ${SST_INSTALL_DIR}/include)
if(FIREDRAKE_HAVE_XRT)
  target_compile_definitions(firedrakecore PRIVATE FIREDRAKE_HAVE_XRT)
  target_link_libraries(firedrakecore PRIVATE XRT::xrt_coreutil)
endif()
set_target_properties(firedrakecore PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${FIREDRAKE_LIB_DIR}"
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN")
install(TARGETS firedrakecore LIBRARY DESTINATION ${FIREDRAKE_INSTALL_LIBDIR})

add_library(firedrake SHARED FiredrakeComponent.cc)
target_link_libraries(firedrake PRIVATE firedrakecore)
firedrake_element_library(firedrake)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include/firedrake)
install(PROGRAMS ${CMAKE_SOURCE_DIR}/scripts/firedrake-gen.py DESTINATION share/firedrake/scripts)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/templates DESTINATION share/firedrake)
install(FILES ${CMAKE_SOURCE_DIR}/cmake/Firedrake.cmake DESTINATION share/firedrake/cmake)

# EOF
