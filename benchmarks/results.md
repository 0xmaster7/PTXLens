# Measured PTXLens results

Source commit: `3e98158ae5abb1da080b5201dcf56e783382948e`; date: 2026-10-20T08:15:15.532889+00:00
Environment: Linux-6.17.0-1022-azure-x86_64-with-glibc2.39; AMD EPYC 7763 64-Core Processor; g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0

In-memory source -> tokenizer -> parser -> static analysis; one warmup, 15 steady_clock samples of 10 analyses; microseconds per analysis. Excludes disk reads and JSON serialization. Synthetic small=100 and large=5000 two-instruction repetitions plus ret.

Annotated corpus: 8 files, 9 kernels, 103 instructions; 103/103 classification agreement (100.0%). 11 categories; 63 recognized opcode families. Passed 20 parser/analysis cases and rejected 21 malformed inputs. Failures: 0.
Timing corpus: 10 files and 10305 instructions including two synthetic throughput workloads. Synthetic instructions are excluded from the classification denominator.

| Fixture | Instructions | Median µs | Min–max µs | Instructions/s |
|---|---:|---:|---:|---:|
| branches.ptx | 14 | 42.32 | 37.51–44.98 | 330819 |
| formatting.ptx | 9 | 43.67 | 35.60–64.26 | 206083 |
| matrix.ptx | 18 | 64.86 | 60.01–72.78 | 277537 |
| memory.ptx | 10 | 40.82 | 37.58–62.02 | 244994 |
| multiple.ptx | 7 | 37.86 | 37.14–45.24 | 184868 |
| reduction.ptx | 25 | 77.48 | 74.02–84.83 | 322652 |
| unknown.ptx | 2 | 10.18 | 10.09–13.27 | 196442 |
| vector_add.ptx | 18 | 64.16 | 59.83–76.21 | 280531 |
| synthetic_small.ptx | 201 | 334.13 | 323.77–395.42 | 601557 |
| synthetic_large.ptx | 10001 | 17706.00 | 17284.40–20111.10 | 564838 |

Reproduce with `python3 benchmarks/benchmark.py build/ptxlens`. Raw samples and SHA-256 fixture digests are in results.json.

## Limitations

- No CUDA compiler, ptxas, NVIDIA GPU, or GPU execution was available.
- Fixtures follow official PTX ISA syntax; no claim of assembler validation, register allocation, occupancy, divergence, spilling, or GPU performance.
- The intentionally unknown opcode fixture tests retention, not ISA validity.
- Agreement is only against the committed hand-annotated corpus, not the complete PTX ISA.
- Source commit identifies measured code; results are committed afterward.
