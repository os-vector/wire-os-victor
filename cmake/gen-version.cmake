#
# gen-version.cmake
#
# Copyright Anki, Inc. 2018
#
# Generates a files containing info in  <build>/etc/version

#
# <build>/etc/version
#

#
# The version number format is:
#
# X.Y.Z<build_type>[_<build_tag>]
# 
# X = major version
# Y = minor version
# Z = incremental build number
# build_type = dev (d) or empty (release)
# build_tag = optional tag specifier for build

# read base version from VERSION file
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/ANKI_VERSION
  ${CMAKE_SOURCE_DIR}/VICTOR_COMPAT_VERSION)
file(READ ${CMAKE_SOURCE_DIR}/ANKI_VERSION BASE_VERSION)
string(STRIP ${BASE_VERSION} BASE_VERSION)

# ANKI_BUILD_VERSION  contains the build counter
set(ANKI_BUILD_VERSION 0)
if (DEFINED ENV{ANKI_BUILD_VERSION})
    set(ANKI_BUILD_VERSION $ENV{ANKI_BUILD_VERSION})
endif()

# default to "dev" build flavor
set(ANKI_BUILD_TYPE "d")
if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
    set(ANKI_BUILD_TYPE "")
endif()

# if ANKI_BUILD_TAG is defined, append it to the version number with `_` prefex
set(ANKI_BUILD_TAG "")
if (DEFINED ENV{ANKI_BUILD_TAG})
    set(ANKI_BUILD_VERSION "_$ENV{ANKI_BUILD_TAG}")
endif()

configure_file(${CMAKE_SOURCE_DIR}/templates/cmake/version.in
               ${CMAKE_BINARY_DIR}/etc/version
               @ONLY)

# Set the VICTOR_COMPAT_VERSION from the file
file(READ ${CMAKE_SOURCE_DIR}/VICTOR_COMPAT_VERSION VICTOR_COMPAT_VERSION)
string(STRIP ${VICTOR_COMPAT_VERSION} VICTOR_COMPAT_VERSION)

configure_file(${CMAKE_SOURCE_DIR}/templates/cmake/victor-compat-version.in
               ${CMAKE_BINARY_DIR}/etc/victor-compat-version
               @ONLY)
