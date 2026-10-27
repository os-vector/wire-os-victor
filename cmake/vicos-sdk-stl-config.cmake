# Copy shared STL files to Android Studio output directory so they can be
# packaged in the APK.
# Usage:
#
#   find_package(ndk-stl REQUIRED)
#
# or
#
#   find_package(ndk-stl REQUIRED PATHS ".")

function(configure_shared_stl lib_path so_lib)
  message(STATUS "Configuring STL ${so_lib} for vicos")
  set(src "${VICOS_SDK}/${lib_path}/${so_lib}")
  set(dst "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${so_lib}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${src}")
  if("${src}" IS_NEWER_THAN "${dst}")
    file(COPY_FILE "${src}" "${dst}")
  endif()
endfunction()

configure_shared_stl("sysroot/usr/lib" "libc++.so.1")
configure_shared_stl("sysroot/usr/lib" "libc++abi.so.1")
