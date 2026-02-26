#!/usr/bin/env bash
# Build the C++ simulator executable and the simulation Python module, then run the Python smoke test.
# Run from the project root (where store.yaml and the scripts/ directory live).
# Usage: ./scripts/build_and_test.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

BUILD_DIR="build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring with CMake..."
cmake ../cxx

echo "Building..."
cmake --build . --config Release 2>/dev/null || cmake --build .

cd "$PROJECT_ROOT"

# Python module is typically in build/ on single-config generators
export PYTHONPATH="${BUILD_DIR}${PYTHONPATH:+:$PYTHONPATH}"

echo "Running Python smoke test..."
python tests/test_simulation.py

echo "Done: simulator and simulation module built, smoke test passed."
