cmake_minimum_required(VERSION 3.16)
project(ddsmon LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# ---------------------------------------------------------------- dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(DDSC REQUIRED IMPORTED_TARGET CycloneDDS)

find_program(IDLC_EXECUTABLE NAMES idlc)
if(NOT IDLC_EXECUTABLE)
  message(FATAL_ERROR "idlc not found. Install Eclipse Cyclone DDS: sudo apt install cyclonedds-dev cyclonedds-tools")
endif()

# ---------------------------------------------------------------- IDL codegen
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
set(IDL_FILE ${CMAKE_SOURCE_DIR}/idl/ddsmon.idl)
set(GEN_SOURCES ${GEN_DIR}/ddsmon.c)

add_custom_command(
  OUTPUT ${GEN_DIR}/ddsmon.c ${GEN_DIR}/ddsmon.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
  COMMAND ${IDLC_EXECUTABLE} -l c -o ${GEN_DIR} ${IDL_FILE}
  DEPENDS ${IDL_FILE}
  COMMENT "idlc: generating C types from idl/ddsmon.idl"
  VERBATIM)

add_library(ddsmon_types STATIC ${GEN_SOURCES})
target_include_directories(ddsmon_types PUBLIC ${GEN_DIR})
target_link_libraries(ddsmon_types PUBLIC PkgConfig::DDSC)

# ---------------------------------------------------------------- core library
add_library(ddsmon_core STATIC
  src/util.cpp
  src/json.cpp
  src/types.cpp
  src/config.cpp
  src/stats.cpp
  src/qos_model.cpp
  src/topology.cpp
  src/monitor.cpp
  src/engine.cpp
  src/rules.cpp
  src/report.cpp
  src/publisher.cpp
  src/metrics.cpp
)
target_include_directories(ddsmon_core PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${GEN_DIR}
)
target_link_libraries(ddsmon_core PUBLIC ddsmon_types PkgConfig::DDSC pthread)

# ---------------------------------------------------------------- executables
add_executable(ddsmon src/cli.cpp)
target_link_libraries(ddsmon PRIVATE ddsmon_core)

add_executable(ddsmon-sim src/sim.cpp)
target_link_libraries(ddsmon-sim PRIVATE ddsmon_core)

# ---------------------------------------------------------------- tests
enable_testing()
foreach(t test_json test_stats test_qos test_rules)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE ddsmon_core)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

install(TARGETS ddsmon ddsmon-sim RUNTIME DESTINATION bin)
install(FILES config/ddsmon.json DESTINATION share/ddsmon)
