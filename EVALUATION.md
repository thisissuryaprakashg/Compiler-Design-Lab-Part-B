# Evaluation

## 1. Custom Test Suite (21 tests)

We developed 21 MPI test programs covering all four error categories. Each test is designed to trigger a specific bug class or verify the absence of false positives.

### Type Mismatch Tests
- **test_01** — `float*` buffer with `MPI_INT` → detected
- **test_11** — `double*` buffer with `MPI_FLOAT` → detected
- **test_12** — `int*` buffer with `MPI_INT` → clean (correct program, no false positive)

### Buffer Overlap Tests
- **test_05** — `MPI_Isend` buffer reused before `MPI_Wait` → detected
- **test_13** — Partial overlap of Isend buffer range → detected
- **test_14** — Non-overlapping Isend buffers → clean (no false positive)

### Collective Ordering Tests
- **test_06** — Rank 0 calls `MPI_Bcast`, rank 1 calls `MPI_Reduce` → detected
- **test_15** — Different root arguments in `MPI_Reduce` → detected
- **test_18** — Extra collective on one rank → detected
- **test_19** — `MPI_Reduce` vs `MPI_Allreduce` mismatch → detected
- **test_20** — Correct sequence of Bcast + Reduce + Barrier → clean (no false positive)
- **test_21** — Conditional collective (only one rank calls Bcast) → detected

### Deadlock Tests
- **test_07** — Send-Send deadlock (both ranks send, neither receives) → detected
- **test_08** — Correct Send/Recv pattern → clean (no false positive)
- **test_09** — 3-rank circular deadlock (0→1→2→0) → detected
- **test_10** — 2-rank deadlock with large buffers → detected
- **test_16** — 4-rank circular deadlock → detected
- **test_17** — Correct ring communication with MPI_Sendrecv → clean (no false positive)
- **test_22** — 4-rank halo exchange deadlock → detected

### Multi-Error Tests
- **test_23** — Type mismatch + collective ordering in one program → both detected
- **test_24** — Type mismatch + buffer overlap in one program → both detected

**Result: 21/21 tests — 100% detection rate, 0 false positives.**

---

## 2. MPI-CorrBench External Benchmark

MPI-CorrBench is a public benchmark from TU Darmstadt containing independently-authored MPI programs with known bugs. We tested all in-scope cases (bugs matching our four detection categories).

### Type Mismatch
- `ArgMismatch-MPIRecv-Type-1.c` — `char*` as `MPI_DOUBLE` → detected
- `ArgMismatch-MPIRecv-Type-2.c` — Send `MPI_INT`, Recv `MPI_CHAR` → detected
- `ArgMismatch-MPIRecv-Type-7.c` — `int*` as `MPI_CHAR` → detected
- `ArgMismatch-MPIGather-Type-1.c` — `int*` as `MPI_CHAR` in Gather → detected

### Deadlock
- `MisplacedCall-MPIRecv-Deadlock-1.c` — Recv-Recv deadlock → detected
- `MisplacedCall-MPIRecv-Deadlock-2.c` — Tag-ordering deadlock → detected
- `MisplacedCall-MPIRecv-Deadlock-4.c` — Send-Send deadlock → detected
- `MissingCall-MPISend-Deadlock.c` — Missing Send, Recv waits forever → detected
- `MissingCall-MPIRecv.c` — Missing Recv, Send waits forever → detected

### Collective Ordering
- `MisplacedCall-MPIBarrier-Deadlock-1.c` — Barrier vs Bcast ordering → detected
- `MisplacedCall-MPIBarrier-Deadlock-2.c` — Barrier vs Send ordering → detected
- `ArgMismatch-MPIReduce-root.c` — Different root across ranks → detected
- `MissingCall-MPIGather-Deadlock.c` — Missing Gather on one rank → detected
- `MissingCall-MPIReduce-Deadlock.c` — Missing Reduce on one rank → detected

### Correct Programs (Negative Tests)
- `correct/pt2pt/simple.c` — clean
- `correct/pt2pt/sendrecv.c` — clean

**Result: 16/16 in-scope tests — 100% detection rate, 0 false positives on an external benchmark.**

---

## 3. CoMD Evaluation (ECP Proxy App)

CoMD is a ~3000-line molecular dynamics proxy application from the Department of Energy's ECP-copa project. It uses `MPI_Send`, `MPI_Recv`, `MPI_Bcast`, `MPI_Allreduce`, and halo exchange patterns across multiple source files.

### Compilation
CoMD compiled successfully with `mpiasan-cc` as a drop-in replacement for `mpicc`. No source code modifications were needed.

### Runtime
Ran on 4 MPI ranks with EAM potential, 10×10×10 lattice, 5 timesteps.

- **Baseline (mpicc):** 0.454s
- **Instrumented (mpiasan-cc):** 0.431s
- **Overhead:** < 5% (within measurement noise)
- **False positives:** 0

This confirms the sanitizer is compatible with real-world MPI code and does not introduce false positives or significant overhead.

---

## 4. Baseline Comparison

### Without Sanitizer (`mpicc`)
- Type mismatches: **undetected** — program runs with silent data corruption
- Buffer overlaps: **undetected** — data race causes non-deterministic results
- Collective ordering: **undetected** — program hangs indefinitely with no diagnostic
- Deadlocks: **undetected** — program hangs indefinitely with no diagnostic

### With Sanitizer (`mpiasan-cc`)
- Type mismatches: **detected** — error message with file, line, buffer type, and MPI type
- Buffer overlaps: **detected** — error message with overlapping address ranges
- Collective ordering: **detected** — error message with mismatched kind/root/sequence per rank
- Deadlocks: **detected** — error message with cycle path (e.g., `0 → 1 → 2 → 0`)

### vs MUST (RWTH Aachen)
- MUST cannot detect buffer-type mismatches (no access to compile-time types)
- MUST requires separate installation and Java runtime
- Our tool is a single `mpiasan-cc` wrapper — zero setup beyond building

---

## 5. Summary

- **Custom tests:** 21/21 passed (100%)
- **External benchmark (MPI-CorrBench):** 16/16 passed (100%)
- **Real application (CoMD):** 0 false positives, < 5% overhead
- **Detection categories:** 4 (type mismatch, buffer overlap, collective ordering, deadlock)
- **Deadlock patterns:** Send-Send, Recv-Recv, tag-ordering, missing-call, N-rank circular
