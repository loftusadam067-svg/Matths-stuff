# GCSE math solver

A hybrid LLM + symbolic-executor solver for GCSE-level mathematics, designed
to run on a Raspberry Pi Zero 2 W (512 MB RAM, ARM Cortex-A53). The intended
chassis is a modified Casio fx-991CW.

## Architecture

```
problem (text)
   |
   v
+-------------+      [CALC]...[/CALC]      +-----------------+
| LLM engine  |  ------------------------> |  tag extractor  |
| (llama.cpp) |                            +-----------------+
+-------------+                                    |
                                                   v
                              +----------------------------------+
                              |  executor engine                 |
                              |    - ExprTk   (single line)      |
                              |    - Lua 5.4  (multi-step, loops)|
                              +----------------------------------+
                                                   |
                                                   v
                                          +-----------------+
                                          | output formatter|
                                          +-----------------+
```

The LLM is asked to emit a single `[CALC] ... [/CALC]` block. If the block
contains a single expression, ExprTk evaluates it; if it contains a Lua
script (multi-line, control flow, `math.*`, `return`), Lua 5.4 runs it in a
sandbox with a hard 2-second timeout. On evaluation failure, the LLM is
re-prompted with the error and the original code, then evaluated once more.

## Quick start

```bash
cd gcse-solver
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/solver --model /path/to/model.gguf "Solve 3x + 7 = 22"
```

Expected output:

```
[PROB]: Solve 3x + 7 = 22
[CODE]: (22 - 7) / 3
[ANS]:  5.0
```

For full build instructions (including cross-compiling for ARM64), see
[BUILD.md](BUILD.md).

## CLI

```
solver --model <path> [--repl] [--verbose] [problem]
```

| Flag        | Meaning                                                    |
| ----------- | ---------------------------------------------------------- |
| `--model`   | Path to a GGUF model file (required).                      |
| `--repl`    | Interactive read-eval-print loop.                          |
| `--verbose` | Diagnostic logging to stderr.                              |
| `-h`        | Print usage and exit.                                      |

A problem can be passed as the final argument(s), or piped on stdin.

## Example queries

- `Solve 2x + 5 = 13`
- `Calculate 15% of 200`
- `Find the area of a circle with radius 5 cm`
- `Solve x^2 + 5x + 6 = 0`
- `In a triangle, angle A = 30 degrees, angle B = 60 degrees. Find angle C`
- `Find the gradient of y = x^2 + 3x at x = 2`

## FAQ

**Why two execution engines?**
ExprTk is fast and safe for arithmetic expressions, but cannot run
multi-step procedures. Lua 5.4 fills that gap with sandboxing and a
deterministic instruction-count hook for timeouts.

**How is the LLM kept from hallucinating?**
The system prompt forbids prose; only `[CALC]...[/CALC]` is accepted. The
extractor rejects malformed output. Failed evaluations trigger a single
self-correction pass with the original problem, the failing code, and the
error message.

**Why is the model not bundled?**
Models are large and licensing varies. The user supplies one via `--model`.

## Layout

```
gcse-solver/
├── CMakeLists.txt        # standalone CMake project, links parent llama.cpp
├── BUILD.md              # detailed build / cross-compile / runtime docs
├── README.md             # this file
├── src/
│   ├── main.cpp          # CLI, REPL, error-recovery loop
│   ├── llm_engine.{hpp,cpp}      # llama.cpp wrapper
│   ├── tag_extractor.hpp         # [CALC]...[/CALC] parser
│   ├── executor.{hpp,cpp}        # ExprTk + Lua sandbox
│   └── output_formatter.hpp      # [PROB]/[CODE]/[ANS] printer
└── tests/
    └── test_executor.cpp # Doctest unit tests
```

## Notes

This is a private fork of llama.cpp augmented with the solver in
`gcse-solver/`. Per the upstream contributor policy, AI-assisted
contributions to llama.cpp itself are restricted; this directory is
additive and lives outside the upstream module structure.
