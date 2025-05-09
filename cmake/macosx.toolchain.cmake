set(MACOSX_COMPILER_FLAGS)
set(MACOSX_COMPILER_FLAGS_CXX)
set(MACOSX_COMPILER_FLAGS_DEBUG)
set(MACOSX_COMPILER_FLAGS_RELEASE)
set(MACOSX_LINKER_FLAGS)
set(MACOSX_LINKER_FLAGS_EXE)
set(MACOSX)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build arch" FORCE)


# Generic flags.
list(APPEND MACOSX_COMPILER_FLAGS
  -arch x86_64 -DMACOSX=1 -DSIMULATOR=1 -Wformat -Werror=format-security
  -fstack-protector-strong -Wno-deprecated-declarations -Wno-strict-prototypes 
  -Wno-non-c-typedef-for-linkage -Wno-defaulted-function-deleted -Wno-dangling-gsl
  -Wno-tautological-overlap-compare)
list(APPEND MACOSX_COMPILER_FLAGS_RELEASE
  -D_FORTIFY_SOURCE=2)

# Convert these lists into strings.
string(REPLACE ";" " " MACOSX_COMPILER_FLAGS         "${MACOSX_COMPILER_FLAGS}")
string(REPLACE "-arch arm64" " " MACOSX_COMPILER_FLAGS         "${MACOSX_COMPILER_FLAGS}")
string(REPLACE ";" " " MACOSX_COMPILER_FLAGS_CXX     "${MACOSX_COMPILER_FLAGS_CXX}")
string(REPLACE "-arch arm64" " " MACOSX_COMPILER_FLAGS_CXX     "${MACOSX_COMPILER_FLAGS_CXX}")
string(REPLACE ";" " " MACOSX_COMPILER_FLAGS_DEBUG   "${MACOSX_COMPILER_FLAGS_DEBUG}")
string(REPLACE ";" " " MACOSX_COMPILER_FLAGS_RELEASE "${MACOSX_COMPILER_FLAGS_RELEASE}")
string(REPLACE ";" " " MACOSX_LINKER_FLAGS           "${MACOSX_LINKER_FLAGS}")
string(REPLACE ";" " " MACOSX_LINKER_FLAGS_EXE       "${MACOSX_LINKER_FLAGS_EXE}")

# Set or retrieve the cached flags.
# This is necessary in case the user sets/changes flags in subsequent
# configures. If we included the Android flags in here, they would get
# overwritten.
set(CMAKE_C_FLAGS ""
  CACHE STRING "Flags used by the compiler during all build types.")
set(CMAKE_CXX_FLAGS ""
  CACHE STRING "Flags used by the compiler during all build types.")
set(CMAKE_C_FLAGS_DEBUG ""
  CACHE STRING "Flags used by the compiler during debug builds.")
set(CMAKE_CXX_FLAGS_DEBUG ""
  CACHE STRING "Flags used by the compiler during debug builds.")
set(CMAKE_C_FLAGS_RELEASE ""
  CACHE STRING "Flags used by the compiler during release builds.")
set(CMAKE_CXX_FLAGS_RELEASE ""
  CACHE STRING "Flags used by the compiler during release builds.")
set(CMAKE_MODULE_LINKER_FLAGS ""
  CACHE STRING "Flags used by the linker during the creation of modules.")
set(CMAKE_SHARED_LINKER_FLAGS ""
  CACHE STRING "Flags used by the linker during the creation of dll's.")
set(CMAKE_EXE_LINKER_FLAGS ""
  CACHE STRING "Flags used by the linker.")

set(CMAKE_C_FLAGS             "${MACOSX_COMPILER_FLAGS} ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS           "${MACOSX_COMPILER_FLAGS} ${MACOSX_COMPILER_FLAGS_CXX} ${CMAKE_CXX_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG       "${MACOSX_COMPILER_FLAGS_DEBUG} ${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_DEBUG     "${MACOSX_COMPILER_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_RELEASE     "${MACOSX_COMPILER_FLAGS_RELEASE} ${CMAKE_C_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASE   "${MACOSX_COMPILER_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_SHARED_LINKER_FLAGS "${MACOSX_LINKER_FLAGS} ${CMAKE_SHARED_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "${MACOSX_LINKER_FLAGS} ${CMAKE_MODULE_LINKER_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS    "${MACOSX_LINKER_FLAGS} ${MACOSX_LINKER_FLAGS_EXE} ${CMAKE_EXE_LINKER_FLAGS}")
set(MACOSX 1)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

option(USE_ANKIASAN "Enable address sanitizer" OFF)
if (USE_ANKIASAN)
  set(ASAN_CXX_FLAGS           PRIVATE
                               -fno-omit-frame-pointer
                               -fsanitize=address
  )

  set(ASAN_LINKER_FLAGS        PRIVATE
                               -fsanitize=address
  )
endif()
