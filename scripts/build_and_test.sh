#!/usr/bin/env bash
# Build PriceRiot C++ targets (simulator + simulation Python module) and run tests.
# Usage:
#   ./scripts/build_and_test.sh             # full build (visualizer + python module)
#   ./scripts/build_and_test.sh --headless  # headless Python module only (no SFML/ImGui)

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

HEADLESS=0
for arg in "$@"; do
  if [ "$arg" = "--headless" ]; then
    HEADLESS=1
  fi
done

PYTHON_BIN="python3"
CMAKE_BIN="cmake"
if [ -x "$PROJECT_ROOT/.venv/bin/python" ]; then
  PYTHON_BIN="$PROJECT_ROOT/.venv/bin/python"
fi
if [ -x "$PROJECT_ROOT/.venv/bin/cmake" ]; then
  CMAKE_BIN="$PROJECT_ROOT/.venv/bin/cmake"
fi

CMAKE_ARGS=("-DPython_EXECUTABLE=$PYTHON_BIN" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5")
if [ "$HEADLESS" -eq 1 ]; then
  CMAKE_ARGS+=("-DBUILD_VISUALIZER=OFF")
fi
PYBIND_DIR="$("$PYTHON_BIN" -m pybind11 --cmakedir 2>/dev/null || true)"
if [ -n "$PYBIND_DIR" ]; then
  CMAKE_ARGS+=("-Dpybind11_DIR=$PYBIND_DIR")
fi

BUILD_DIR="build"
mkdir -p "$BUILD_DIR"

echo "Configuring with CMake..."
"$CMAKE_BIN" -S cxx -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"

echo "Building..."
"$CMAKE_BIN" --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"

export PYTHONPATH="${BUILD_DIR}:${PROJECT_ROOT}/python:${PROJECT_ROOT}${PYTHONPATH:+:$PYTHONPATH}"

echo "Running Python tests..."
if "$PYTHON_BIN" -m pytest --version >/dev/null 2>&1; then
  "$PYTHON_BIN" -m pytest -v tests/
else
  "$PYTHON_BIN" tests/test_simulation.py
fi

echo "Done: build and tests passed."
