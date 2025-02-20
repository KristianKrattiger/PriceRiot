from setuptools import setup, Extension
import pybind11

ext_modules = [
    Extension(
        "mycppmodule",
        ["src/cxx/src/module.cpp"],
        include_dirs=[pybind11.get_include()],
        language="c++",
    ),
]

setup(
    name="mycppmodule",
    ext_modules=ext_modules,
)
