#include <pybind11/pybind11.h>

namespace py = pybind11;

int multiply(int a, int b) {
    return a * b;
}

PYBIND11_MODULE(mycppmodule, m) {
    m.def("multiply", &multiply, "Multiply two numbers");
}
