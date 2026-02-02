cmake_minimum_required(VERSION 3.14)
project(PriceRiot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Fix for the FetchContent_Populate deprecation warning ---
cmake_policy(SET CMP0169 OLD)

include(FetchContent)

# --- 1. Fetch YAML-CPP ---
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

# --- 2. Fetch SFML ---
FetchContent_Declare(
        SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 2.6.1
)
FetchContent_MakeAvailable(SFML)

# --- 3. Fetch Dear ImGui ---
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.89.9
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# --- 4. Fetch ImGui-SFML ---
set(IMGUI_DIR ${imgui_SOURCE_DIR})

# !!! CRITICAL FIX !!!
# We tell ImGui-SFML NOT to try to find_package(SFML),
# because we already have the SFML targets from step 2.
set(IMGUI_SFML_FIND_SFML OFF)

FetchContent_Declare(
        ImGui-SFML
        GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git
        GIT_TAG 2.6.x
)
FetchContent_MakeAvailable(ImGui-SFML)

# --- 5. Sources ---
set(COMMON_SOURCES
        environment/products.cpp
        environment/shelf.cpp
        agents/customer.cpp
        environment/cell.cpp
        engine/transaction.cpp
        agents/basket.cpp
        agents/customer_behavior.cpp
        environment/environment.cpp
        environment/store_init.cpp
        tools/io_csv.cpp
        environment/store_inventory.cpp
        environment/store_layout.cpp
        environment/navmesh.cpp
        environment/navmesh_generator.cpp
        environment/navmesh_pathfinder.cpp
        environment/physics.cpp
        environment/physics_generator.cpp
        environment/collision_manager.cpp
        engine/navmesh_visualizer.cpp
)

# Conditionally add MinGW _setjmp workaround
set(SIMULATOR_SOURCES
        engine/sim.cpp
        ${COMMON_SOURCES}
)

# Add MinGW _setjmp fix if using MinGW/GCC on Windows
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND SIMULATOR_SOURCES tools/mingw_setjmp_fix.c)
    message(STATUS "Adding MinGW _setjmp workaround for SFML FreeType compatibility")
endif()

add_executable(simulator ${SIMULATOR_SOURCES})

target_include_directories(simulator PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/agents
        ${CMAKE_CURRENT_SOURCE_DIR}/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/environment
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
)

# --- 6. Linking ---
target_link_libraries(simulator PRIVATE
        yaml-cpp
        ImGui-SFML::ImGui-SFML
        sfml-graphics
        sfml-system
        sfml-window
)

# MinGW: SFML's bundled FreeType references _setjmp; link mingwex to resolve it.
# This is needed for FreeType which uses setjmp/longjmp
# SFML's FreeType is compiled for MSVCRT runtime, so we need to ensure compatibility
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Detected MinGW/GCC on Windows - fixing _setjmp reference for SFML FreeType")
    
    # Get the MinGW library directory
    get_filename_component(MINGW_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)
    get_filename_component(MINGW_ROOT_DIR ${MINGW_BIN_DIR} DIRECTORY)
    set(MINGW_LIB_DIR "${MINGW_ROOT_DIR}/lib")
    message(STATUS "MinGW lib directory: ${MINGW_LIB_DIR}")
    
    # Try to find mingwex which provides _setjmp/_longjmp
    # Search in MinGW lib directory and standard paths
    find_library(MINGWEX_LIB 
        NAMES mingwex libmingwex
        PATHS 
        ${MINGW_LIB_DIR}
        ${MINGW_ROOT_DIR}/${CMAKE_C_COMPILER_TARGET}/lib
        ${CMAKE_C_COMPILER}/../../lib
        ${CMAKE_C_COMPILER}/../../${CMAKE_C_COMPILER_TARGET}/lib
    )
    
    if(MINGWEX_LIB)
        target_link_libraries(simulator PRIVATE ${MINGWEX_LIB})
        message(STATUS "Found mingwex at: ${MINGWEX_LIB}")
    else()
        # Add MinGW lib directory to linker search path
        target_link_directories(simulator PRIVATE ${MINGW_LIB_DIR})
        # Link by name - linker will search the added directory
        target_link_libraries(simulator PRIVATE mingwex)
        message(STATUS "Linking mingwex by name (searching in ${MINGW_LIB_DIR})")
    endif()
    
    # Force linker to include mingwex by adding it as a linker option
    # This ensures it's linked even if find_library didn't locate it
    target_link_options(simulator PRIVATE "-lmingwex")
endif()

target_compile_definitions(simulator PRIVATE YAML_CPP_STATIC_DEFINE)