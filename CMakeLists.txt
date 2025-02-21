cmake_minimum_required(VERSION 3.15)
project(PriceRiot)

set(CMAKE_CXX_STANDARD 17)

# Add pybind11 as a subdirectory
add_subdirectory(pybind11)

# Create the executable target for your simulation
add_executable(PriceRiot_app src/cxx/src/sim.cpp)
target_include_directories(PriceRiot_app PRIVATE ${PROJECT_SOURCE_DIR}/src/cxx/include)

# Create the pybind11 module target
add_library(mycppmodule MODULE src/cxx/src/module.cpp)
target_link_libraries(mycppmodule PRIVATE pybind11::module)
