if (VICOS)
  anki_build_cxx_library(gpio ${ANKI_SRCLIST_DIR} STATIC)

  target_include_directories(gpio 
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
  )

  set_target_properties(gpio PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
    "${CMAKE_SOURCE_DIR}"
  )

  set_target_properties(gpio PROPERTIES LINKER_LANGUAGE C)

  if (STANDALONE_SIM)
    target_compile_options(gpio PRIVATE ${STANDALONE_SIM_NOWARN})
  endif()

  anki_build_strip(TARGET gpio)

  anki_build_target_license(gpio "ANKI")

endif()

