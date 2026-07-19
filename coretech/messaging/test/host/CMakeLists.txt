#
# coretech/messaging/test/host/CMakeLists.txt
#
# Native (Linux x86_64) host build of the cti_messaging unit tests.
#
# Standalone CMake project, NOT part of the main victor cross-compile graph —
# same pattern as coretech/planning/test/host/ (see that CMakeLists for the
# full rationale). LocalUdpServer/LocalUdpClient are plain POSIX local-domain
# socket code, so they exercise the identical kernel behavior (unix DGRAM
# flow control, EAGAIN on a full peer queue) on the host as on VICOS.
#
# USAGE
# -----
#   scripts/run-host-tests.sh                  # builds + runs all host suites
# or manually:
#   cmake -S coretech/messaging/test/host -B _build/host-messaging -DREPO_ROOT=$(pwd)
#   cmake --build _build/host-messaging -j
#   ctest --test-dir _build/host-messaging --output-on-failure
#
cmake_minimum_required(VERSION 3.16)
project(cti_messaging_host_tests CXX)

if(NOT DEFINED REPO_ROOT)
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
endif()
message(STATUS "cti_messaging host tests: REPO_ROOT = ${REPO_ROOT}")

if(NOT EXISTS "${REPO_ROOT}/coretech/messaging/shared/LocalUdpServer.cpp")
  message(FATAL_ERROR
    "REPO_ROOT (${REPO_ROOT}) does not look like a wire-os-victor checkout. "
    "Pass -DREPO_ROOT=/path/to/wire-os-victor.")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CT    "${REPO_ROOT}/coretech")
set(UTIL  "${REPO_ROOT}/lib/util/source")
set(GTEST "${REPO_ROOT}/lib/das-client/testing/gtest/fused-src")

include_directories(
  ${REPO_ROOT}
  ${UTIL}/anki
  ${GTEST}
)

# logging.h requires exactly one of CORETECH_ROBOT/ENGINE/SHARED to pick a log
# level; the messaging sources live in the shared tier.
add_compile_definitions(CORETECH_SHARED)

add_executable(cti_messaging_test
  ${CT}/messaging/shared/LocalUdpServer.cpp
  ${CT}/messaging/shared/LocalUdpClient.cpp
  ${CT}/messaging/shared/SocketUtils.cpp
  ${CT}/messaging/test/testLocalUdpServer.cpp
  ${CT}/messaging/test/testLocalUdpClient.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/logging_stubs.cpp
  ${GTEST}/gtest/gtest-all.cc
  ${GTEST}/gtest/gtest_main.cc
)

find_package(Threads REQUIRED)
target_link_libraries(cti_messaging_test PRIVATE Threads::Threads)

enable_testing()
add_test(NAME cti_messaging_test COMMAND cti_messaging_test)
set_tests_properties(cti_messaging_test PROPERTIES
  ENVIRONMENT "GTEST_OUTPUT=xml:${CMAKE_BINARY_DIR}/ctiMessagingGoogleTest.xml"
  TIMEOUT 120
)
