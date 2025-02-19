#[[
   Copyright 2020-2021 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
]]

find_package(absl CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

get_filename_component(SILKWORM_MAIN_DIR ../ ABSOLUTE)

file(GLOB_RECURSE SILKWORM_NODE_SRC CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.c" "*.h" "*.cc")
list(FILTER SILKWORM_NODE_SRC EXCLUDE REGEX "_test\.cpp$")

set(SILKWORM_INTERFACE_SRC
        ${SILKWORM_MAIN_DIR}/interfaces/sentry.grpc.pb.cc
        ${SILKWORM_MAIN_DIR}/interfaces/sentry.grpc.pb.h
        ${SILKWORM_MAIN_DIR}/interfaces/sentry.pb.cc
        ${SILKWORM_MAIN_DIR}/interfaces/sentry.pb.h
        ${SILKWORM_MAIN_DIR}/interfaces/types.pb.cc
        ${SILKWORM_MAIN_DIR}/interfaces/types.pb.h)

add_library(silkworm_node ${SILKWORM_NODE_SRC} ${SILKWORM_INTERFACE_SRC})

add_dependencies(silkworm_node generate_sentry_grpc)

set_source_files_properties(${SILKWORM_INTERFACE_SRC} PROPERTIES GENERATED TRUE)
if(NOT MSVC)
  set_source_files_properties(${SILKWORM_INTERFACE_SRC} PROPERTIES COMPILE_FLAGS -Wno-sign-conversion)
endif(MSVC)

target_include_directories(silkworm_node PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SILKWORM_MAIN_DIR}
        ${SILKWORM_MAIN_DIR}/magic_enum/include)

set(SILKWORM_NODE_PUBLIC_LIBS silkworm_core mdbx-static absl::flat_hash_map absl::flat_hash_set absl::btree roaring
        nlohmann_json::nlohmann_json gRPC::grpc++ protobuf::libprotobuf)
set(SILKWORM_NODE_PRIVATE_LIBS cborcpp)

if(MSVC)
  list(APPEND SILKWORM_NODE_PRIVATE_LIBS ntdll.lib)
endif(MSVC)

target_link_libraries(silkworm_node PUBLIC ${SILKWORM_NODE_PUBLIC_LIBS} PRIVATE ${SILKWORM_NODE_PRIVATE_LIBS})
