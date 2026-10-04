function(anki_build_go)
  cmake_parse_arguments(ANKI "NATIVE" "NAME;DIR;OUTPUT_NAME;BUILD_TAGS" "SRC_DIRS;INCLUDE_DIRS;LINK_FLAGS;PKG_CONFIG_PATHS;EXTRA_DEPENDS" ${ARGN})
  if(NOT ANKI_NAME OR NOT ANKI_DIR)
    message(FATAL_ERROR "anki_build_go: NAME and DIR are required")
  endif()

  set(output_name ${ANKI_OUTPUT_NAME})
  if(NOT output_name)
    set(output_name ${ANKI_NAME})
  endif()

  find_program(GO_EXECUTABLE NAMES ${ANKI_GO_COMPILER} REQUIRED)
  message(STATUS "anki_build_go: using Go ${ANKI_GO_COMPILER}")
  find_program(UPX_EXECUTABLE NAMES ${ANKI_UPX})
  if(UPX_EXECUTABLE)
    message(STATUS "anki_build_go: using UPX ${ANKI_UPX}")
  else()
    message(WARNING "anki_build_go: UPX not found. It's recommended to have upx installed to reduce Go binary sizes.")
  endif()

  get_filename_component(go_project_dir ${ANKI_DIR} ABSOLUTE)
  if(ANKI_NATIVE)
    set(go_out_dir ${CMAKE_BINARY_DIR}/native)
  else()
    set(go_out_dir ${CMAKE_BINARY_DIR})
  endif()
  set(go_out ${go_out_dir}/${output_name})
  set(stamp_file ${go_out}.built)

  file(GLOB_RECURSE go_srcs CONFIGURE_DEPENDS ${go_project_dir}/*.go)
  set(go_build_paths "")
  if(ANKI_SRC_DIRS)
    foreach(d IN LISTS ANKI_SRC_DIRS)
      set(abs ${go_project_dir}/${d})
      if(IS_DIRECTORY ${abs})
        file(RELATIVE_PATH rel ${go_project_dir} ${abs})
        list(APPEND go_build_paths ./${rel})
      elseif(EXISTS ${abs})
        file(RELATIVE_PATH rel ${go_project_dir} ${abs})
        list(APPEND go_build_paths ${rel})
      else()
        message(FATAL_ERROR "anki_build_go: invalid SRC_DIRS: ${d}")
      endif()
    endforeach()
  else()
    list(APPEND go_build_paths ./...)
  endif()

  if(ANKI_NATIVE)
    find_program(ANKI_NATIVE_CC NAMES cc gcc clang REQUIRED NO_CMAKE_FIND_ROOT_PATH)
    find_program(ANKI_NATIVE_CXX NAMES c++ g++ clang++ REQUIRED NO_CMAKE_FIND_ROOT_PATH)
    message(STATUS "anki_build_go: ${output_name} builds for the host with ${ANKI_NATIVE_CC}")
    set(go_env
      CGO_ENABLED=1
      GOOS=linux
      CC=${ANKI_NATIVE_CC}
      CXX=${ANKI_NATIVE_CXX}
    )
  else()
    set(go_env
      CGO_ENABLED=1
      GOOS=linux
      GOARCH=arm
      GOARM=7
      CC=${CMAKE_C_COMPILER}
      CXX=${CMAKE_CXX_COMPILER}
    )
  endif()
  set(go_c_flags "")
  if(ANKI_INCLUDE_DIRS)
    foreach(i IN LISTS ANKI_INCLUDE_DIRS)
      string(APPEND go_c_flags "-I${i} ")
    endforeach()
  endif()
  #string(APPEND go_c_flags "-Wno-implicit-function-declaration ")
  list(APPEND go_env "CGO_CFLAGS=${go_c_flags}")
  if(ANKI_PKG_CONFIG_PATHS)
    list(JOIN ANKI_PKG_CONFIG_PATHS ":" pkg_paths_str)
    list(APPEND go_env "PKG_CONFIG_PATH=${pkg_paths_str}")
  endif()
  if(ANKI_LINK_FLAGS)
    string(REPLACE ";" " " link_str ${ANKI_LINK_FLAGS})
    list(APPEND go_env "CGO_LDFLAGS=${link_str}")
  endif()

  set(build_flags "-modcacherw")
  if(ANKI_BUILD_TAGS)
    list(APPEND build_flags "-tags" "${ANKI_BUILD_TAGS}")
  endif()
  list(APPEND build_flags "-ldflags" "-s -w")

  set(cmds
    COMMAND ${CMAKE_COMMAND} -E make_directory ${go_out_dir}
    COMMAND ${CMAKE_COMMAND} -E env ${go_env} ${GO_EXECUTABLE} mod download -modcacherw
    COMMAND ${CMAKE_COMMAND} -E env ${go_env} ${GO_EXECUTABLE} build -o ${go_out} ${build_flags} ${go_build_paths}
  )
  if(UPX_EXECUTABLE)
    list(APPEND cmds
      COMMAND ${CMAKE_SOURCE_DIR}/tools/build/tools/upx-if-packed.sh "${ANKI_UPX}" "${go_out}"
    )
  endif()
  list(APPEND cmds COMMAND ${CMAKE_COMMAND} -E touch ${stamp_file})

  add_custom_command(
    OUTPUT ${stamp_file}
    ${cmds}
    WORKING_DIRECTORY ${go_project_dir}
    DEPENDS ${go_srcs} ${go_project_dir}/go.mod ${go_project_dir}/go.sum ${ANKI_EXTRA_DEPENDS}
    COMMENT "Building Go target ${output_name}"
    VERBATIM
  )
  add_custom_target(${ANKI_NAME} ALL DEPENDS ${stamp_file})
  if(NOT ANKI_NATIVE)
    install(PROGRAMS ${go_out} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
  endif()
endfunction()