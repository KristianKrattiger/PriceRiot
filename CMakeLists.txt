cmake_minimum_required(VERSION 3.15)
project(PriceRiot_cpp)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(pybind11)

add_library(mycppmodule MODULE src/cxx/src/module.cpp)
target_link_libraries(mycppmodule PRIVATE pybind11::module)

target_include_directories(PriceRiot_cpp PRIVATE ${PROJECT_SOURCE_DIR}/cxx/include)

# Only compile sim.cpp for the executable.
add_executable(PriceRiot_cpp src/cxx/src/sim.cpp)
