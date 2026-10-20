# PTXLens

A C++17 static analyzer for a documented subset of NVIDIA PTX assembly. It tokenizes PTX, identifies entry points and functions, classifies instructions, constructs intraprocedural control-flow graphs, and reports evidence-backed observations. It helps inspect compiler-facing GPU assembly without requiring a GPU.

## Build and use

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
build/ptxlens analyze fixtures/vector_add.ptx
build/ptxlens stats fixtures/vector_add.ptx
build/ptxlens cfg fixtures/branches.ptx
build/ptxlens analyze fixtures/formatting.ptx --json
python3 benchmarks/benchmark.py build/ptxlens
```

Requires CMake, Python 3 for tests, and a C++17 compiler. No CUDA dependency. Configure a separate Debug directory with `-DENABLE_SANITIZERS=ON` for ASan/UBSan. CI runs Release and sanitized Debug configurations, independent of GPU availability.

Captured output from `cfg fixtures/branches.ptx`:

```text
PTX 8.0 target=sm_70 address_size=64
entry branches: 14 instructions, 6 blocks, 2 predicated, 4 branches
  B0 [0,4) -> B1 B4
  B1 [4,7) -> B2 B3
  B2 [7,9) -> B5
  B3 [9,11) -> B5
  B4 [11,12) -> B5
  B5 [12,14) ->
```

Instruction indices are zero-based; ranges are half-open. JSON includes each instruction's source line, full opcode, modifiers, predicate, operand tokens, category, memory space, declarations, counts, blocks/edges, and observations. All three public commands accept `--json` and return the full analysis schema. Malformed or unsupported structural syntax exits 1 with an error.

## Supported syntax and analysis

- Required `.version`, `.target sm_*`, and `.address_size 32|64` directives.
- `.entry` bodies; `.func` bodies, return/argument parameter lists, and external function declarations. `.visible`, `.weak`, and `.extern` qualifiers.
- Scalar `.reg`, `.param`, `.shared`, `.local`, `.global`, and `.const` declarations; register ranges such as `%r<16>` mean 16 declared variables. Storage arrays, alignment annotations, and initializer braces are recognized. Unsized storage arrays report zero elements (unknown extent).
- Labels, nested brace scopes, bracketed memory operands, vector operand braces, semicolon statements spanning lines, multiple statements on a line, `@p`/`@!p` predication, quoted strings, and inline/multiline comments.
- `.file`/`.loc` are physical-line directives; semicolon directives inside bodies are retained separately. Function tuning directives before the body are retained.
- Eleven categories: arithmetic, logical, data movement, global/shared/local memory, control flow, synchronization, atomics, conversion, and other/unknown. Rule families are listed in `classes()` in `src/ptxlens.cpp`; unknown opcodes remain visible and count toward totals.
- Counts by category/address space, declared register variables by type, register-reference occurrences, unique textual register references, predicated instructions, branches, basic blocks, and direct CFG edges.

Register-reference counts include predicate uses, operands, and PTX special registers such as `%tid.x`. They exclude declarations. Names beginning with `%` are counted lexically; declared named registers without `%` are also recognized. Unique names are per function, so shadowed names share a textual identity. Counts are **not hardware register allocation**. `ld.param`, `ld.const`, and generic memory accesses use the data-movement category and retain their address space; atomics retain the atomic category and address space.

Direct `bra` targets must resolve to labels within the function. Predicated branches/returns retain fallthrough edges. Indirect `brx` edges are explicitly unresolved; calls retain the intraprocedural continuation, without callee edges. CFGs are structural, without reachability pruning or predicate-value analysis.

## Architecture and observations

`include/ptxlens.hpp` defines token, declaration, instruction, function, CFG, and report structures. `src/ptxlens.cpp` separates comment-aware tokenization, structured parsing, opcode-family rules, CFG construction, and analysis. `src/main.cpp` provides text/JSON output and an in-process benchmark. Files are limited to 64 MiB; scope nesting to 128; declaration extents to one million. RAII containers own all parser data.

Observations include global-memory-heavy static mixes, branch-heavy static mixes, repeated loads with identical normalized syntactic addresses, more than 128 declared virtual registers, and scalar self-moves. Every observation provides a line/instruction, evidence, explanation, and confidence/limitation. Repeated load addresses can observe different values; static branches do not establish divergence. No occupancy, coalescing, spilling, bottleneck, speedup, or hardware timing claim is made.

## Independent tests and measurements

```sh
python3 tests/test.py build/ptxlens
python3 benchmarks/benchmark.py build/ptxlens --smoke
```

The hand-labeled opcode/category sequences in [fixtures/annotations.json](fixtures/annotations.json) were written from source fixtures, independently of analyzer output. The test compares full ordered sequences, not just totals. All labeled instructions (including returns and the intentionally unknown opcode) contribute once to the agreement denominator. Tests additionally check declarations, predicates, memory spaces, manually expected CFG edges, source lines, observations, malformed syntax, and a comment-insertion metamorphic transformation.

Fixtures cover vector addition, a shared-memory reduction, scalar dot-product arithmetic, branches, local/shared/global operations, predication, formatting, multiple entry points, and unknown syntax retention. They are illustrative hand-authored PTX, not compiler-generated or GPU-executed benchmark kernels. `futureop.experimental.b32` is deliberately outside the ISA to test preservation.

Recorded counts, independent-label agreement, latency/throughput medians and variability: [benchmarks/results.md](benchmarks/results.md); raw samples and fixture hashes: [results.json](benchmarks/results.json). Timing uses one warmup and 15 batches of ten in-memory tokenize/parse/analyze calls; disk I/O and JSON output are excluded. Two explicitly synthetic throughput workloads are excluded from classification agreement. Reproduction is one benchmark command after building; CI checks correctness without performance thresholds.

## Limitations and reference

This is not a PTX assembler, a complete grammar implementation, or a GPU simulator. Opcode modifiers, target compatibility, type checking, register-use legality, and complete operand semantics are not validated. Basic arities are checked for a documented set of common families. Debug `.section` payloads, multidimensional storage declarations, macros, repeated declaration/definition names, and arbitrary top-level directives are outside the supported subset. Nested lexical declarations are counted, without full scope-sensitive name binding. The parser preserves unknown instructions but rejects unsupported top-level structures explicitly.

CUDA, `ptxas`, and an NVIDIA GPU were unavailable in the measurement environment. There was no assembler validation or GPU execution. Classification agreement applies only to the independently labeled corpus. Manual syntax review uses NVIDIA's official [PTX ISA reference](https://docs.nvidia.com/cuda/parallel-thread-execution/) (retrieved 2026-10-20), specifically [syntax](https://docs.nvidia.com/cuda/parallel-thread-execution/#syntax), [state spaces](https://docs.nvidia.com/cuda/parallel-thread-execution/#state-spaces), [entry points/functions](https://docs.nvidia.com/cuda/parallel-thread-execution/#kernel-and-function-directives), and [instruction descriptions](https://docs.nvidia.com/cuda/parallel-thread-execution/#instructions). NVIDIA's specification remains the authority; this tool implements a bounded inspection subset.
