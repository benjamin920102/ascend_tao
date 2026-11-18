cmake_minimum_required(VERSION 3.18)
project(ascend_sn14 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Run CANN's set_env.sh before configuring. Override with -DASCEND_HOME_PATH=...
set(ASCEND_HOME_PATH "$ENV{ASCEND_HOME_PATH}" CACHE PATH "Installed CANN toolkit root")
if(NOT ASCEND_HOME_PATH)
  set(ASCEND_HOME_PATH "/usr/local/Ascend/ascend-toolkit/latest")
endif()
if(NOT EXISTS "${ASCEND_HOME_PATH}/include/acl/acl.h")
  message(FATAL_ERROR "CANN headers not found at ${ASCEND_HOME_PATH}; source <CANN>/set_env.sh or set -DASCEND_HOME_PATH=...")
endif()
foreach(lib ascendcl nnopbase opapi_nn)
  if(NOT EXISTS "${ASCEND_HOME_PATH}/lib64/lib${lib}.so")
    message(FATAL_ERROR "Missing CANN library ${ASCEND_HOME_PATH}/lib64/lib${lib}.so")
  endif()
endforeach()

add_executable(ascend_sn14 src/main.cpp)
target_include_directories(ascend_sn14 PRIVATE "${ASCEND_HOME_PATH}/include")
target_link_libraries(ascend_sn14 PRIVATE
  "${ASCEND_HOME_PATH}/lib64/libascendcl.so"
  "${ASCEND_HOME_PATH}/lib64/libnnopbase.so"
  "${ASCEND_HOME_PATH}/lib64/libopapi_nn.so")
set_target_properties(ascend_sn14 PROPERTIES BUILD_RPATH "${ASCEND_HOME_PATH}/lib64")
