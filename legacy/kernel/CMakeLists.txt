cmake_minimum_required(VERSION 3.16)
set(CMAKE_ASC_ARCHITECTURES "dav-2201" CACHE STRING "Ascend ISA for the selected NPU")
find_package(ASC REQUIRED)
project(ascend_tao LANGUAGES ASC CXX)

add_library(ascend_tao SHARED tao_pow.asc ../native/tao_host_crypto.cpp)
target_include_directories(ascend_tao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(ascend_tao PROPERTIES POSITION_INDEPENDENT_CODE ON)
# CANN 9.x prefers libacl_rt; older installations may expose libascendcl.
find_library(TAO_ACL_RUNTIME NAMES acl_rt ascendcl
    HINTS "$ENV{CANN_HOME}/lib64" "${ASCEND_CANN_PACKAGE_PATH}/lib64")
if(NOT TAO_ACL_RUNTIME)
  message(FATAL_ERROR "CANN ACL runtime library not found (libacl_rt/libascendcl)")
endif()
target_link_libraries(ascend_tao PRIVATE tiling_api register platform m dl pthread ${TAO_ACL_RUNTIME})
target_compile_options(ascend_tao PRIVATE
  $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

# All mining loops and multi-device scheduling execute in native C++.
find_package(Threads REQUIRED)
add_executable(tao_native ../native/tao_native.cpp)
target_compile_features(tao_native PRIVATE cxx_std_17)
target_include_directories(tao_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tao_native PRIVATE ascend_tao Threads::Threads)
set_target_properties(tao_native PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
