cmake_minimum_required(VERSION 3.14)
project(PriceRiot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_policy(SET CMP0169 OLD)

include(FetchContent)

option(BUILD_VISUALIZER "Build the SFML/ImGui desktop visualizer" ON)

# --- 1. YAML-CPP (system or FetchContent) ---
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(
            yaml-cpp
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG 0.8.0
    )
    FetchContent_MakeAvailable(yaml-cpp)
    # yaml-cpp 0.8.0 omits <cstdint> in emitterutils.cpp, which fails on GCC 15+/16+
    if(TARGET yaml-cpp AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(yaml-cpp PRIVATE -include cstdint)
    endif()
endif()

# --- 2-4. SFML, Dear ImGui, ImGui-SFML (visualizer only) ---
if(BUILD_VISUALIZER)
    FetchContent_Declare(
            SFML
            GIT_REPOSITORY https://github.com/SFML/SFML.git
            GIT_TAG 2.6.1
    )
    FetchContent_MakeAvailable(SFML)

    FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.89.9
    )
    FetchContent_GetProperties(imgui)
    if(NOT imgui_POPULATED)
        FetchContent_Populate(imgui)
    endif()

    set(IMGUI_DIR ${imgui_SOURCE_DIR})
    set(IMGUI_SFML_FIND_SFML OFF)

    FetchContent_Declare(
            ImGui-SFML
            GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git
            GIT_TAG 2.6.x
    )
    FetchContent_MakeAvailable(ImGui-SFML)
endif()

# --- 5. Sources ---
set(COMMON_SOURCES
        environment/products.cpp
        environment/shelf.cpp
        agents/customer.cpp
        agents/worker.cpp
        environment/cell.cpp
        engine/transaction.cpp
        agents/basket.cpp
        agents/customer_behavior.cpp
        engine/task_manager.cpp
        environment/environment.cpp
        environment/spatial_hash.cpp
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
        environment/checkout_queue.cpp
        engine/navmesh_visualizer.cpp
        engine/behavior_log.cpp
)

# --- 6. Desktop Visualizer (optional; disabled with -DBUILD_VISUALIZER=OFF) ---
if(BUILD_VISUALIZER)
    set(SIMULATOR_SOURCES
            engine/sim.cpp
            engine/simulator.cpp
            ${COMMON_SOURCES}
    )

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

    target_link_libraries(simulator PRIVATE
            yaml-cpp
            ImGui-SFML::ImGui-SFML
            sfml-graphics
            sfml-system
            sfml-window
    )

    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "Detected MinGW/GCC on Windows - fixing _setjmp reference for SFML FreeType")
        get_filename_component(MINGW_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)
        get_filename_component(MINGW_ROOT_DIR ${MINGW_BIN_DIR} DIRECTORY)
        set(MINGW_LIB_DIR "${MINGW_ROOT_DIR}/lib")
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
        else()
            target_link_directories(simulator PRIVATE ${MINGW_LIB_DIR})
            target_link_libraries(simulator PRIVATE mingwex)
        endif()
        target_link_options(simulator PRIVATE "-lmingwex")
    endif()

    target_compile_definitions(simulator PRIVATE YAML_CPP_STATIC_DEFINE)
endif()

# ──────────────────────────────────────────────────────────────────────────────
# Python extension module: simulation
# Built without SFML/ImGui (headless analytics binding only).
# Configure with the Python you intend to use at runtime, e.g.:
#   cmake ../cxx -DPython_EXECUTABLE="C:/Python311/python.exe"
# ──────────────────────────────────────────────────────────────────────────────
find_package(Python COMPONENTS Interpreter Development.Module QUIET)

if(Python_FOUND)
    find_package(pybind11 CONFIG QUIET)
    if(NOT pybind11_FOUND)
        FetchContent_Declare(
                pybind11
                GIT_REPOSITORY https://github.com/pybind/pybind11.git
                GIT_TAG        v2.13.6
        )
        FetchContent_MakeAvailable(pybind11)
    endif()

    # navmesh_visualizer.cpp uses full SFML graphics — exclude from the headless module.
    set(HEADLESS_SOURCES ${COMMON_SOURCES})
    list(REMOVE_ITEM HEADLESS_SOURCES engine/navmesh_visualizer.cpp)

    set(BINDING_SOURCES
            bindings/priceriot_bindings.cpp
            engine/simulator.cpp
            ${HEADLESS_SOURCES}
    )

    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND BINDING_SOURCES tools/mingw_setjmp_fix.c)
    endif()

    pybind11_add_module(simulation ${BINDING_SOURCES})

    target_include_directories(simulation PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/agents
            ${CMAKE_CURRENT_SOURCE_DIR}/engine
            ${CMAKE_CURRENT_SOURCE_DIR}/environment
            ${CMAKE_CURRENT_SOURCE_DIR}/tools
            ${CMAKE_CURRENT_SOURCE_DIR}/bindings
    )

    target_link_libraries(simulation PRIVATE yaml-cpp)
    target_compile_definitions(simulation PRIVATE YAML_CPP_STATIC_DEFINE PRICERIOT_HEADLESS)

    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(MINGWEX_LIB)
            target_link_libraries(simulation PRIVATE ${MINGWEX_LIB})
        else()
            target_link_directories(simulation PRIVATE ${MINGW_LIB_DIR})
            target_link_libraries(simulation PRIVATE mingwex)
        endif()
        target_link_options(simulation PRIVATE "-lmingwex")

        get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
        foreach(dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
            if(EXISTS "${MINGW_BIN_DIR}/${dll}")
                add_custom_command(TARGET simulation POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_BIN_DIR}/${dll}"
                        "$<TARGET_FILE_DIR:simulation>/${dll}"
                )
            endif()
        endforeach()
    endif()

    message(STATUS "Python ${Python_VERSION} found — 'simulation' module will be built")
else()
    message(WARNING "Python Development headers not found — skipping 'simulation' module. "
                    "Pass -DPython_EXECUTABLE=<path> to select a specific interpreter.")
endif()
