#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "tbb" for configuration "Release"
set_property(TARGET tbb APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(tbb PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libtbb.so"
  IMPORTED_SONAME_RELEASE "libtbb.so"
  )

list(APPEND _cmake_import_check_targets tbb )
list(APPEND _cmake_import_check_files_for_tbb "${_IMPORT_PREFIX}/lib/libtbb.so" )

# Import target "opencv_core" for configuration "Release"
set_property(TARGET opencv_core APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_core PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "tbb"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopencv_core.so.4.12.0"
  IMPORTED_SONAME_RELEASE "libopencv_core.so.412"
  )

list(APPEND _cmake_import_check_targets opencv_core )
list(APPEND _cmake_import_check_files_for_opencv_core "${_IMPORT_PREFIX}/lib/libopencv_core.so.4.12.0" )

# Import target "opencv_flann" for configuration "Release"
set_property(TARGET opencv_flann APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_flann PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopencv_flann.so.4.12.0"
  IMPORTED_SONAME_RELEASE "libopencv_flann.so.412"
  )

list(APPEND _cmake_import_check_targets opencv_flann )
list(APPEND _cmake_import_check_files_for_opencv_flann "${_IMPORT_PREFIX}/lib/libopencv_flann.so.4.12.0" )

# Import target "opencv_imgproc" for configuration "Release"
set_property(TARGET opencv_imgproc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_imgproc PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopencv_imgproc.so.4.12.0"
  IMPORTED_SONAME_RELEASE "libopencv_imgproc.so.412"
  )

list(APPEND _cmake_import_check_targets opencv_imgproc )
list(APPEND _cmake_import_check_files_for_opencv_imgproc "${_IMPORT_PREFIX}/lib/libopencv_imgproc.so.4.12.0" )

# Import target "opencv_ml" for configuration "Release"
set_property(TARGET opencv_ml APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_ml PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopencv_ml.so.4.12.0"
  IMPORTED_SONAME_RELEASE "libopencv_ml.so.412"
  )

list(APPEND _cmake_import_check_targets opencv_ml )
list(APPEND _cmake_import_check_files_for_opencv_ml "${_IMPORT_PREFIX}/lib/libopencv_ml.so.4.12.0" )

# Import target "opencv_features2d" for configuration "Release"
set_property(TARGET opencv_features2d APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_features2d PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopencv_features2d.so.4.12.0"
  IMPORTED_SONAME_RELEASE "libopencv_features2d.so.412"
  )

list(APPEND _cmake_import_check_targets opencv_features2d )
list(APPEND _cmake_import_check_files_for_opencv_features2d "${_IMPORT_PREFIX}/lib/libopencv_features2d.so.4.12.0" )

# Import target "opencv_imgcodecs" for configuration "Release"
set_property(TARGET opencv_imgcodecs APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_imgcodecs PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopencv_imgcodecs.so.4.12.0"
  IMPORTED_SONAME_RELEASE "libopencv_imgcodecs.so.412"
  )

list(APPEND _cmake_import_check_targets opencv_imgcodecs )
list(APPEND _cmake_import_check_files_for_opencv_imgcodecs "${_IMPORT_PREFIX}/lib/libopencv_imgcodecs.so.4.12.0" )

# Import target "opencv_calib3d" for configuration "Release"
set_property(TARGET opencv_calib3d APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_calib3d PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopencv_calib3d.so.4.12.0"
  IMPORTED_SONAME_RELEASE "libopencv_calib3d.so.412"
  )

list(APPEND _cmake_import_check_targets opencv_calib3d )
list(APPEND _cmake_import_check_files_for_opencv_calib3d "${_IMPORT_PREFIX}/lib/libopencv_calib3d.so.4.12.0" )

# Import target "opencv_gapi" for configuration "Release"
set_property(TARGET opencv_gapi APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_gapi PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "tbb"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopencv_gapi.so.4.12.0"
  IMPORTED_SONAME_RELEASE "libopencv_gapi.so.412"
  )

list(APPEND _cmake_import_check_targets opencv_gapi )
list(APPEND _cmake_import_check_files_for_opencv_gapi "${_IMPORT_PREFIX}/lib/libopencv_gapi.so.4.12.0" )

# Import target "opencv_objdetect" for configuration "Release"
set_property(TARGET opencv_objdetect APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_objdetect PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopencv_objdetect.so.4.12.0"
  IMPORTED_SONAME_RELEASE "libopencv_objdetect.so.412"
  )

list(APPEND _cmake_import_check_targets opencv_objdetect )
list(APPEND _cmake_import_check_files_for_opencv_objdetect "${_IMPORT_PREFIX}/lib/libopencv_objdetect.so.4.12.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
