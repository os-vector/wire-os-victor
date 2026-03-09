#
# lib/signalEssence/CMakeLists.txt
#

cmake_minimum_required(VERSION 3.10)

project(signal_essence_proj)

option(SE_V009 "Enable use of new Signal Essence v009 libs" ON)
option(SE_HIGHRES "Enable Signal Essence High Resolution Echo Cancelling" ON)

include(signalessence)

# Create an interface library that provides include paths and links the prebuilt lib
add_library(signal_essence INTERFACE)

target_link_libraries(signal_essence
  INTERFACE
  ${SIGNALESSENCE_LIBS}
)

target_include_directories(signal_essence
  INTERFACE
  ${SIGNALESSENCE_INCLUDE_PATHS}
)

target_compile_definitions(signal_essence
  INTERFACE
  SE_V009=$<BOOL:${SE_V009}>
)

anki_build_target_license(signal_essence "ANKI" "Commercial")
