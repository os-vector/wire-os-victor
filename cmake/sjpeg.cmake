if (VICOS)
  set(SJPEG_INCLUDE_PATH "${ANKI_THIRD_PARTY_DIR}/sjpeg/vicos/include")
  set(SJPEG_LIB_PATH "${ANKI_THIRD_PARTY_DIR}/sjpeg/vicos/lib")
endif()

set(SJPEG_LIBS sjpeg)

add_library(sjpeg STATIC IMPORTED)

set_target_properties(sjpeg PROPERTIES
  IMPORTED_LOCATION "${SJPEG_LIB_PATH}/libsjpeg.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SJPEG_INCLUDE_PATH}")
anki_build_target_license(sjpeg "Apache-2.0,${CMAKE_SOURCE_DIR}/licenses/sjpeg.license")
