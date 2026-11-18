cmake_minimum_required(VERSION 3.16)
set(CMAKE_ASC_ARCHITECTURES "dav-2201" CACHE STRING "ASC target architecture")
find_package(ASC REQUIRED)
project(taohash_npu LANGUAGES ASC CXX)

add_library(taohash_npu SHARED sha256d.asc ../src/sha256d.cpp)
target_include_directories(taohash_npu PRIVATE ../include)
set_target_properties(taohash_npu PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  LINKER_LANGUAGE ASC
  BUILD_RPATH "$ORIGIN")
# Only the ACL runtime is used. We do NOT invoke the op tiling registration
# API or PlatformAscendC, so -ltiling_api/-lregister/-lplatform are wrong.
find_library(TH_ACL_LIBRARY NAMES acl_rt ascendcl
  HINTS "$ENV{CANN_HOME}/lib64" "${ASCEND_CANN_PACKAGE_PATH}/lib64"
        "$ENV{CANN_HOME}/aarch64-linux/lib64" "$ENV{CANN_HOME}/x86_64-linux/lib64")
if(NOT TH_ACL_LIBRARY)
  message(FATAL_ERROR "CANN ACL runtime not found (acl_rt/ascendcl): verify CANN_HOME")
endif()
target_link_libraries(taohash_npu PRIVATE ${TH_ACL_LIBRARY} m dl pthread)
target_compile_options(taohash_npu PRIVATE
  $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>)
