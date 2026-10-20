# Measured PTXLens results

Source commit: `73cae30f47e55da9554796cdfcff88a9ca07da2f`; date: 2026-10-20T09:03:47.837304+00:00
Environment: Linux-6.17.0-1022-azure-x86_64-with-glibc2.39; AMD EPYC 7763 64-Core Processor; g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0

In-memory source -> tokenizer -> parser -> static analysis; one warmup, 15 steady_clock samples of 10 analyses; microseconds per analysis. Excludes disk reads and JSON serialization. Synthetic small=100 and large=5000 two-instruction repetitions plus ret.

Annotated corpus: 8 files, 9 kernels, 103 instructions; 103/103 classification agreement (100.0%). 11 categories; 63 recognized opcode families. Passed 20 parser/analysis cases and rejected 21 malformed inputs. Failures: 0.
Timing corpus: 10 files and 10305 instructions including two synthetic throughput workloads. Synthetic instructions are excluded from the classification denominator.

| Fixture | Instructions | Median µs | Min–max µs | Instructions/s |
|---|---:|---:|---:|---:|
| branches.ptx | 14 | 75.96 | 67.36–245.51 | 184305 |
| formatting.ptx | 9 | 67.94 | 36.48–78.07 | 132464 |
| matrix.ptx | 18 | 111.16 | 97.23–121.57 | 161929 |
| memory.ptx | 10 | 69.78 | 62.68–183.03 | 143298 |
| multiple.ptx | 7 | 67.49 | 57.44–85.19 | 103722 |
| reduction.ptx | 25 | 152.79 | 140.53–186.14 | 163627 |
| unknown.ptx | 2 | 16.16 | 16.04–19.61 | 123730 |
| vector_add.ptx | 18 | 120.56 | 106.05–128.22 | 149306 |
| synthetic_small.ptx | 201 | 636.91 | 588.39–804.87 | 315586 |
| synthetic_large.ptx | 10001 | 34864.20 | 32713.10–51270.10 | 286856 |

Reproduce with `python3 benchmarks/benchmark.py build/ptxlens`. Raw samples and SHA-256 fixture digests are in results.json.

## Limitations

- No CUDA compiler, ptxas, NVIDIA GPU, or GPU execution was available.
- Fixtures follow official PTX ISA syntax; no claim of assembler validation, register allocation, occupancy, divergence, spilling, or GPU performance.
- The intentionally unknown opcode fixture tests retention, not ISA validity.
- Agreement is only against the committed hand-annotated corpus, not the complete PTX ISA.
- Source commit identifies measured code; results are committed afterward.
