cmake_minimum_required(VERSION 3.20)
project(MPISanitizer)

# Find LLVM
find_package(LLVM 17 REQUIRED CONFIG)
message(STATUS "Found LLVM: ${LLVM_PACKAGE_VERSION}")
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Find MPI
find_package(MPI REQUIRED)
message(STATUS "Found MPI: ${MPI_C_COMPILER}")

# ── Target 1: The LLVM Pass Plugin ────────────────────────────────
# Builds pass.cpp into a shared plugin (.so) that clang loads
add_library(MPISanPass MODULE pass.cpp)
set_target_properties(MPISanPass PROPERTIES PREFIX "")
target_include_directories(MPISanPass PRIVATE ${LLVM_INCLUDE_DIRS})

# ── Target 2: The Runtime Library ─────────────────────────────────
# Builds runtime.cpp into a shared lib linked into the user's program
add_library(mpiasan_rt SHARED runtime.cpp)
target_include_directories(mpiasan_rt PRIVATE ${MPI_C_INCLUDE_DIRS})
target_link_libraries(mpiasan_rt PRIVATE MPI::MPI_C MPI::MPI_CXX)
