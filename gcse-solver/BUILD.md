# Build instructions

The GCSE math solver is a C++20 CMake project that lives in `gcse-solver/`
inside the llama.cpp tree. llama.cpp is built as a subdirectory; the solver
links against the resulting `llama` target.

## Prerequisites

- CMake >= 3.20
- GCC >= 10 or Clang >= 14 (C++20 support)
- `liblua5.4-dev` (preferred) — if not installed, the build fetches Lua sources
- `git` (for FetchContent)

On Debian/Ubuntu/Raspberry Pi OS Lite (64-bit):

```bash
sudo apt update
sudo apt install -y build-essential cmake git liblua5.4-dev
```

## Native build (Linux x86-64 or Raspberry Pi)

```bash
cd gcse-solver
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/solver --model /path/to/model.gguf "Solve 3x + 7 = 22"
```

llama.cpp is configured as a CMake subproject from the parent directory; you
do not need to build it separately.

## Cross-compilation (x86-64 → ARM64 for Pi Zero 2 W)

Install the cross toolchain:

```bash
sudo apt install -y g++-aarch64-linux-gnu
```

Create a minimal toolchain file `aarch64.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER       aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER     aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

Then configure and build:

```bash
cd gcse-solver
cmake -B build-arm64 \
      -DCMAKE_TOOLCHAIN_FILE=aarch64.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm64 -j$(nproc)
```

Copy `build-arm64/solver` to the Pi.

## Model

The solver expects a 4-bit quantized GGUF model in the 1.5B–3B parameter
range. Recommended:

- `Qwen2.5-Math-1.5B-Instruct-Q4_K_M.gguf`
- `Phi-3.5-mini-Q4_K_M.gguf`

Download from Hugging Face, place anywhere, and pass via `--model`.

## Memory budget (Pi Zero 2 W, 512 MB)

| Component         | Approximate footprint |
| ----------------- | --------------------- |
| Quantized model   | ~180 MB               |
| llama.cpp context | ~32 MB (n_ctx = 512)  |
| Executor + Lua    | ~8 MB                 |
| Application heap  | ~16 MB                |
| **Total**         | **~236 MB**           |

## Runtime

```bash
./build/solver --model model.gguf "Solve 2x + 5 = 15"
./build/solver --model model.gguf --repl
./build/solver --model model.gguf --verbose "Find sin(30 degrees)"

# Read problem from stdin
echo "Calculate 15% of 200" | ./build/solver --model model.gguf
```

## Tests

```bash
cd gcse-solver
cmake -B build -DGCSE_BUILD_TESTS=ON
cmake --build build --target test_executor
./build/test_executor

# Or run via ctest:
ctest --test-dir build --output-on-failure
```
