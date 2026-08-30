#
# cloud/CMakeLists.txt
#
cmake_minimum_required(VERSION 3.10)

project(cloud)

include(anki_build_cxx)
include(anki_build_util)
include(breakpad)
include(webots)

include(anki_build_go)

set(OPUS_INC   "${CMAKE_SOURCE_DIR}/3rd/opus/vicos/include/opus")
set(OPUS_LIBS  "-L${CMAKE_SOURCE_DIR}/3rd/opus/vicos/lib")
set(OPUS_PKGCONFIG  "${CMAKE_SOURCE_DIR}/3rd/opus/vicos/lib/pkgconfig")

#
# Gateway Protbuf and GRPC configuration
#

# Grab the gateway files
file(STRINGS "${ANKI_SRCLIST_DIR}/gateway_grpc.srcs.lst" GATEWAY_GRPC_SRCS)
file(STRINGS "${ANKI_SRCLIST_DIR}/gateway_proto.srcs.lst" GATEWAY_PROTO_SRCS)

# exclude grpc file from proto sources
list(FILTER GATEWAY_PROTO_SRCS EXCLUDE REGEX ".*external_interface.proto")

set(GATEWAY_GRPC_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/generated/go/src/proto/external_interface")
set(GATEWAY_GRPC_GO_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/internal/proto/external_interface")
set(PROTOBUF_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/generated/proto/external_interface")
set(GATEWAY_PROTO_HOME_DIR "${CMAKE_SOURCE_DIR}/tools/protobuf/gateway")
set(GATEWAY_PROTO_HOME_DIR_RELATIVE "../tools/protobuf/gateway")

# Formulate the generated file names
string(REPLACE ".proto" ".pb.go" GATEWAY_PROTO_GO_GEN_SRCS "${GATEWAY_PROTO_SRCS};${GATEWAY_GRPC_SRCS}")
string(REPLACE ".proto" ".pb.gw.go" GATEWAY_GRPC_GO_GEN_SRCS "${GATEWAY_GRPC_SRCS}")
string(REPLACE ".proto" ".pb.h" GATEWAY_PROTO_CPP_GEN_HEADERS "${GATEWAY_PROTO_SRCS}")
string(REPLACE ".proto" ".pb.cc" GATEWAY_PROTO_CPP_GEN_SRCS "${GATEWAY_PROTO_SRCS}")

# and give those generated files the appropriate pathing
string(REPLACE "${GATEWAY_PROTO_HOME_DIR_RELATIVE}/public/" "${GATEWAY_GRPC_OUTPUT_DIR}/" GATEWAY_PROTO_GO_GEN_SRCS "${GATEWAY_PROTO_GO_GEN_SRCS}")
string(REPLACE "${GATEWAY_PROTO_HOME_DIR_RELATIVE}/public/" "${GATEWAY_GRPC_OUTPUT_DIR}/" GATEWAY_GRPC_GO_GEN_SRCS "${GATEWAY_GRPC_GO_GEN_SRCS}")
string(REPLACE "${GATEWAY_PROTO_HOME_DIR_RELATIVE}/public/" "${PROTOBUF_OUTPUT_DIR}/" GATEWAY_PROTO_CPP_GEN_HEADERS "${GATEWAY_PROTO_CPP_GEN_HEADERS}")
string(REPLACE "${GATEWAY_PROTO_HOME_DIR_RELATIVE}/public/" "${PROTOBUF_OUTPUT_DIR}/" GATEWAY_PROTO_CPP_GEN_SRCS "${GATEWAY_PROTO_CPP_GEN_SRCS}")

# Full paths for the proto files
string(REPLACE "${GATEWAY_PROTO_HOME_DIR_RELATIVE}" "${GATEWAY_PROTO_HOME_DIR}/" GATEWAY_GRPC_SRCS "${GATEWAY_GRPC_SRCS}")
string(REPLACE "${GATEWAY_PROTO_HOME_DIR_RELATIVE}" "${GATEWAY_PROTO_HOME_DIR}/" GATEWAY_PROTO_SRCS "${GATEWAY_PROTO_SRCS}")

# Expose the necessary files for the engine to build thease into it
set(GATEWAY_PROTO_CPP_GEN_HEADERS "${GATEWAY_PROTO_CPP_GEN_HEADERS}" PARENT_SCOPE)
set(GATEWAY_PROTO_CPP_GEN_SRCS "${GATEWAY_PROTO_CPP_GEN_SRCS}" PARENT_SCOPE)
set(GATEWAY_PROTO_SRCS "${GATEWAY_PROTO_SRCS}" PARENT_SCOPE)
set(PROTOBUF_OUTPUT_DIR "${PROTOBUF_OUTPUT_DIR}" PARENT_SCOPE)
set(GATEWAY_PROTO_HOME_DIR "${GATEWAY_PROTO_HOME_DIR}" PARENT_SCOPE)

if(STANDALONE_SIM)
  set(VIC_CLOUD_BUILD_TAGS "nolibopusfile")
else()
  set(VIC_CLOUD_BUILD_TAGS "vicos nolibopusfile")
endif()

anki_build_go(
  NAME         vic-cloud
  DIR          "${CMAKE_CURRENT_SOURCE_DIR}"
  SRC_DIRS     cloud
  INCLUDE_DIRS "${OPUS_INC}"
  LINK_FLAGS   "${OPUS_LIBS}"
  PKG_CONFIG_PATHS "${OPUS_PKGCONFIG}"
  BUILD_TAGS "${VIC_CLOUD_BUILD_TAGS}"
)

set(PROTO_SRC_DIR "${GATEWAY_PROTO_HOME_DIR}/public")
file(GLOB PROTO_FILES "${PROTO_SRC_DIR}/*.proto")

set(GENERATED_GO_FILES "")
foreach(proto IN LISTS PROTO_FILES)
  get_filename_component(proto_name ${proto} NAME_WE)
  list(APPEND GENERATED_GO_FILES
    "${GATEWAY_GRPC_GO_OUTPUT_DIR}/${proto_name}.pb.go"
  )
endforeach()

set(PROTO_GO_GEN_FILES
  ${GATEWAY_GRPC_GO_OUTPUT_DIR}/external_interface_grpc.pb.go
  ${GATEWAY_GRPC_GO_OUTPUT_DIR}/external_interface.pb.gw.go
  ${GENERATED_GO_FILES}
)

#anki_build_go(
#  NAME         vic-gateway
#  DIR          "${CMAKE_CURRENT_SOURCE_DIR}"
#  SRC_DIRS     gateway
#  INCLUDE_DIRS "${OPUS_INC}"
#  LINK_FLAGS   "${OPUS_LIBS}"
#  PKG_CONFIG_PATHS "${OPUS_PKGCONFIG}"
#  BUILD_TAGS "vicos nolibopusfile"
#  EXTRA_DEPENDS ${PROTO_GO_GEN_FILES}
#)

# wrapper to add protoc-gen-go stuff to path
add_custom_command(
  OUTPUT ${PROTO_GO_GEN_FILES}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${GATEWAY_GRPC_GO_OUTPUT_DIR}"
  COMMAND "${CMAKE_SOURCE_DIR}/cloud/protoc-wrapper.sh"
    ${ANKI_PROTOC}
    --proto_path=${PROTO_SRC_DIR}
    --go_out=${GATEWAY_GRPC_GO_OUTPUT_DIR}
    --go-grpc_out=${GATEWAY_GRPC_GO_OUTPUT_DIR}
    --grpc-gateway_out=${GATEWAY_GRPC_GO_OUTPUT_DIR}
    --go_opt=paths=source_relative
    --go-grpc_opt=paths=source_relative
    --grpc-gateway_opt=paths=source_relative
    ${PROTO_FILES}
  DEPENDS ${PROTO_FILES}
  COMMENT "Generating all Go proto sources"
  VERBATIM
)

add_custom_target(proto_gen ALL DEPENDS ${PROTO_GO_GEN_FILES})

add_dependencies(vic-cloud proto_gen)

add_custom_command(
    OUTPUT "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libopus.so.0"
    COMMAND ${CMAKE_COMMAND}
    ARGS -E copy_if_different "${CMAKE_SOURCE_DIR}/3rd/opus/vicos/lib/libopus.so.0.10.1" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libopus.so.0"
    COMMENT "copy libopus"
    VERBATIM
)

add_custom_target(opus
  DEPENDS "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libopus.so.0"
)

add_dependencies(vic-cloud opus)
