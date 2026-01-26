from setuptools import setup, Extension
import pybind11

ext_modules = [
    Extension(
        "run_simulation",
        ["src/cxx/src/module.cpp"],
        include_dirs=["src/cxx/include"],
        language="c++",
    ),
]

setup(
    name="mycppmodule",
    ext_modules=ext_modules,
)
