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
        environment/storeLayout.cpp
        environment/navmesh.cpp
        environment/navmesh_generator.cpp
        environment/navmesh_pathfinder.cpp
        engine/navmesh_visualizer.cpp
)

add_executable(simulator
        engine/sim.cpp
        ${COMMON_SOURCES}
)

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

target_compile_definitions(simulator PRIVATE YAML_CPP_STATIC_DEFINE)