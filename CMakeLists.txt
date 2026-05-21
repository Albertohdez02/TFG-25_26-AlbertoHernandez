# =============================================================================
# CMake Configuration for IHTC 2024 Solver
# =============================================================================
# Minimum CMake version for C++20 support
cmake_minimum_required(VERSION 3.16)

# Project definition
project(IHTC2024_Solver
    VERSION 1.0.0
    DESCRIPTION "Integrated Hospital Treatment Scheduling - IHTC 2024 Competition Solver"
    LANGUAGES CXX
)

# =============================================================================
# C++ Standard Configuration
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Compiler Flags
# =============================================================================
# Release optimization flags for performance
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -flto")

# Debug flags with sanitizers
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic -fsanitize=address,undefined")

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")

# =============================================================================
# Threads (used by ACOSolver to parallelize ant colony at iteration level)
# =============================================================================
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# =============================================================================
# Source Files
# =============================================================================
# Entity headers (header-only classes)
set(ENTITY_HEADERS
    src/common/types.h
    src/entities/Patient.h
    src/entities/Occupant.h
    src/entities/Room.h
    src/entities/Nurse.h
    src/entities/Surgeon.h
    src/entities/OperatingTheater.h
    src/entities/ProblemData.h
)

# Solution source files
set(SOLUTION_SOURCES
    src/solution/Solution.cpp
)

set(SOLUTION_HEADERS
    src/solution/Solution.h
)

# Evaluator source files
set(EVALUATOR_SOURCES
    src/evaluator/Evaluator.cpp
    src/evaluator/FeasibilityChecker.cpp
)

# Solver source files
set(SOLVER_SOURCES
    src/solver/LocalSearch.cpp
    src/solver/RandomGenerator.cpp
    src/solver/ACOSolver.cpp
    src/solver/ALNSPerturbation.cpp
    src/solver/CompoundMoves.cpp
)

# =============================================================================
# Library Target
# =============================================================================
# Create static library with all core components
add_library(ihtc_core STATIC
    ${SOLUTION_SOURCES}
    ${EVALUATOR_SOURCES}
    ${SOLVER_SOURCES}
)

# Include directories
target_include_directories(ihtc_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# Link pthread for std::thread used by ACOSolver
target_link_libraries(ihtc_core PUBLIC Threads::Threads)

# =============================================================================
# Main Executable
# =============================================================================
add_executable(ihtc_solver
    src/main.cpp
)
target_link_libraries(ihtc_solver PRIVATE ihtc_core)

# =============================================================================
# Ablation Test Executable
# =============================================================================
add_executable(ihtc_ablation
    src/ablation_test.cpp
)
target_link_libraries(ihtc_ablation PRIVATE ihtc_core)

# =============================================================================
# Solution Evaluator Executable
# =============================================================================
add_executable(ihtc_eval
    src/eval_solution.cpp
)
target_link_libraries(ihtc_eval PRIVATE ihtc_core)

# =============================================================================
# Installation Rules
# =============================================================================
# Install headers
install(FILES ${ENTITY_HEADERS} ${SOLUTION_HEADERS}
    DESTINATION include/ihtc
)

# Install library
install(TARGETS ihtc_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# =============================================================================
# Output directories
# =============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =============================================================================
# Print configuration summary
# =============================================================================
message(STATUS "")
message(STATUS "=== IHTC 2024 Solver Configuration ===")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Compiler:   ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
