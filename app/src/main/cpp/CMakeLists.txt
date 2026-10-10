cmake_minimum_required(VERSION 3.22.1)

project("hidimbistown")

set(TOWN_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(TOWN_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

add_library(
    town
    SHARED
    native-lib.cpp
)

# The under-city primitive headers are compiled straight into libtown.so.
# tools/host_harness/harness.cpp builds the very same headers for the host so
# solve_scripts/solve_hidimbi.py can verify them without a device attached.
set(TOWN_HEADERS
    ${TOWN_CPP_DIR}/town_crypto.h
    ${TOWN_CPP_DIR}/scriptorium_vm.h
    ${TOWN_CPP_DIR}/drum_tower.h
    ${TOWN_CPP_DIR}/courier_ledger.h
    ${TOWN_CPP_DIR}/granary_race.h
)
source_group("headers" FILES ${TOWN_HEADERS})

find_library(
    log-lib
    log
)

find_library(
    android-lib
    android
)

target_link_libraries(
    town
    ${log-lib}
    ${android-lib}
)

# Optional host build of the challenge primitives (QA use only).
if(DEFINED TOWN_BUILD_HOST_HARNESS AND EXISTS ${TOWN_ROOT_DIR}/tools/host_harness/harness.cpp)
    add_executable(town_host_harness ${TOWN_ROOT_DIR}/tools/host_harness/harness.cpp)
    target_compile_features(town_host_harness PRIVATE cxx_std_17)
endif()
