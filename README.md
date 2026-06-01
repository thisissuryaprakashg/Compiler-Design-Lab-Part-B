# Compiler-Integrated MPI Usage Sanitizer

## Assignment

> **Assignment 26 - Compiler-Integrated MPI Usage Sanitizer**
>
> **Description:** An LLVM IR instrumentation pass + runtime that detects common MPI usage errors — type mismatches between send/recv, buffer aliasing, mismatched collectives, and deadlock-prone call orderings.
>
> **Background:** MUST (RWTH Aachen) is the closest existing tool, but it's standalone and Java-based. An LLVM-integrated approach enables compiler-level optimizations like eliding provably-safe checks and leveraging type information unavailable to external tools.
>
> **Objective:** Intercept MPI calls at the IR level, track buffer types/sizes and communication patterns, and flag errors at runtime with call stacks and MPI rank information.
>
> **Deliverables:**
> 1. LLVM pass instrumenting MPI call sites with metadata capture
> 2. Runtime library tracking communication patterns and validating constraints
> 3. Detection of: type mismatches, buffer overlaps, collective ordering violations, potential deadlocks
> 4. Test suite of 15+ MPI programs with seeded bugs
> 5. Evaluation on a real MPI application (e.g., a mini-app from ECP proxy apps)

---

## Prerequisites

- Ubuntu / WSL (tested on Ubuntu 24.04)
- Internet connection (for installing packages)

## Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/thisissuryaprakashg/Compiler-Design-Lab-Part-B.git
cd Compiler-Design-Lab-Part-B
```

### 2. Install Dependencies

```bash
sudo apt update
sudo apt install -y clang-17 llvm-17-dev cmake make openmpi-bin libopenmpi-dev
```

### 3. Build

```bash
mkdir build && cd build
cmake ..
make -j4
cd ..
```

This produces `build/MPISanPass.so` (LLVM pass) and `build/libmpiasan_rt.so` (runtime library).

### 4. Make Wrapper Executable

```bash
chmod +x mpiasan-cc
```

---

## Usage

### Compile and run WITH sanitizer:

```bash
./mpiasan-cc tests/test_01_type_mismatch.c -o /tmp/test_01
mpirun -np 2 /tmp/test_01
```

### Compile and run WITHOUT sanitizer (for comparison):

```bash
mpicc tests/test_01_type_mismatch.c -o /tmp/test_01_normal
mpirun -np 2 /tmp/test_01_normal
```

Without the sanitizer, the bug goes undetected. With the sanitizer, you get:

```
==MPISanitizer== ERROR: [TYPE_MISMATCH] on Rank 0
  Call site : tests/test_01_type_mismatch.c:20
  Detail    : MPI_Send: buffer is 'float*' at compile time but MPI_Datatype is MPI_INT
```

---

## Run All Tests

### Type Mismatch

```bash
./mpiasan-cc tests/test_01_type_mismatch.c -o /tmp/t01 && mpirun -np 2 /tmp/t01
./mpiasan-cc tests/test_11_type_double_as_float.c -o /tmp/t11 && mpirun -np 2 /tmp/t11
./mpiasan-cc tests/test_12_type_correct.c -o /tmp/t12 && mpirun -np 2 /tmp/t12
```

Expected: Error on test_01 and test_11. No error on test_12 (correct program).

### Buffer Overlap

```bash
./mpiasan-cc tests/test_05_buffer_overlap.c -o /tmp/t05 && mpirun -np 2 /tmp/t05
./mpiasan-cc tests/test_13_overlap_partial.c -o /tmp/t13 && mpirun -np 2 /tmp/t13
./mpiasan-cc tests/test_14_overlap_safe.c -o /tmp/t14 && mpirun -np 2 /tmp/t14
```

Expected: Error on test_05 and test_13. No error on test_14 (correct program).

### Collective Ordering

```bash
./mpiasan-cc tests/test_06_collective_order.c -o /tmp/t06 && timeout 10 mpirun -np 2 /tmp/t06
./mpiasan-cc tests/test_15_collective_root.c -o /tmp/t15 && timeout 10 mpirun -np 2 /tmp/t15
./mpiasan-cc tests/test_18_coll_extra.c -o /tmp/t18 && timeout 10 mpirun -np 2 /tmp/t18
./mpiasan-cc tests/test_19_coll_reduce_allreduce.c -o /tmp/t19 && timeout 10 mpirun -np 2 /tmp/t19
./mpiasan-cc tests/test_20_coll_correct_seq.c -o /tmp/t20 && mpirun -np 2 /tmp/t20
./mpiasan-cc tests/test_21_coll_conditional.c -o /tmp/t21 && timeout 10 mpirun -np 2 /tmp/t21
```

Expected: Error on test_06, 15, 18, 19, 21. No error on test_20 (correct program).

### Deadlock Detection

```bash
./mpiasan-cc tests/test_07_deadlock.c -o /tmp/t07 && timeout 10 mpirun -np 2 /tmp/t07
./mpiasan-cc tests/test_08_deadlock_correct.c -o /tmp/t08 && mpirun -np 2 /tmp/t08
./mpiasan-cc tests/test_09_deadlock_circular.c -o /tmp/t09 && timeout 10 mpirun -np 3 /tmp/t09
./mpiasan-cc tests/test_10_deadlock_small.c -o /tmp/t10 && timeout 10 mpirun -np 2 /tmp/t10
./mpiasan-cc tests/test_16_deadlock_4rank.c -o /tmp/t16 && timeout 10 mpirun -np 4 /tmp/t16
./mpiasan-cc tests/test_17_ring_correct.c -o /tmp/t17 && mpirun -np 4 /tmp/t17
./mpiasan-cc tests/test_22_deadlock_halo.c -o /tmp/t22 && timeout 15 mpirun -np 4 /tmp/t22
```

Expected: Error on test_07, 09, 10, 16, 22. No error on test_08 and test_17 (correct programs).

### Multi-Error Combinations

```bash
./mpiasan-cc tests/test_23_combo_scatter.c -o /tmp/t23 && timeout 10 mpirun -np 2 /tmp/t23
./mpiasan-cc tests/test_24_combo_type_overlap.c -o /tmp/t24 && mpirun -np 2 /tmp/t24
```

Expected: Multiple errors per test (type mismatch + collective order, type mismatch + buffer overlap).

---

## Evaluation on ECP Proxy Apps

### CoMD (Classical Molecular Dynamics)

CoMD is a ~3000-line molecular dynamics proxy app from the Department of Energy (ECP-copa). It uses MPI_Send/Recv, MPI_Bcast, MPI_Allreduce, and halo exchange patterns.

```bash
# Download CoMD
git clone https://github.com/ECP-copa/CoMD.git
cd CoMD/src-mpi
cp Makefile.vanilla Makefile

# Build and run BASELINE (normal mpicc)
make CC=mpicc OPTFLAGS='-g -O2' -j4
cd ..
time mpirun -np 4 --oversubscribe ./bin/CoMD-mpi -e -x 10 -y 10 -z 10 -i 2 -j 2 -k 1 -n 5

# Rebuild with SANITIZER
cd src-mpi && make clean
make CC=$(cd ../.. && pwd)/mpiasan-cc OPTFLAGS='-g -O2' -j4
cd ..
time mpirun -np 4 --oversubscribe ./bin/CoMD-mpi -e -x 10 -y 10 -z 10 -i 2 -j 2 -k 1 -n 5
cd ..
```

Expected: Zero false positives, negligible runtime overhead (< 5%).

---

## Evaluation on MPI-CorrBench (External Benchmark)

MPI-CorrBench is a public benchmark from TU Darmstadt with independently-authored MPI programs containing known bugs.

```bash
# Download MPI-CorrBench
git clone --depth 1 https://github.com/tudasc/MPI-Corrbench.git

# Type Mismatch Tests
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/pt2pt/ArgMismatch-MPIRecv-Type-1.c -o /tmp/cb1 && mpirun -np 2 /tmp/cb1
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/pt2pt/ArgMismatch-MPIRecv-Type-2.c -o /tmp/cb2 && mpirun -np 2 /tmp/cb2
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/pt2pt/ArgMismatch-MPIRecv-Type-7.c -o /tmp/cb3 && mpirun -np 2 /tmp/cb3

# Deadlock Tests
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/pt2pt/MisplacedCall-MPIRecv-Deadlock-1.c -o /tmp/cb4 && timeout 10 mpirun -np 2 /tmp/cb4
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/pt2pt/MisplacedCall-MPIRecv-Deadlock-2.c -o /tmp/cb5 && timeout 10 mpirun -np 2 /tmp/cb5
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/pt2pt/MisplacedCall-MPIRecv-Deadlock-4.c -o /tmp/cb6 && timeout 10 mpirun -np 2 /tmp/cb6
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/pt2pt/MissingCall-MPISend-Deadlock.c -o /tmp/cb7 && timeout 10 mpirun -np 2 /tmp/cb7

# Collective Ordering Tests
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/coll/MisplacedCall-MPIBarrier-Deadlock-1.c -o /tmp/cb8 && timeout 10 mpirun -np 2 /tmp/cb8
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/coll/ArgMismatch-MPIReduce-root.c -o /tmp/cb9 && timeout 10 mpirun -np 2 /tmp/cb9
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/coll/ArgMismatch-MPIGather-Type-1.c -o /tmp/cb10 && timeout 10 mpirun -np 2 /tmp/cb10

# Correct Program (must show NO errors)
./mpiasan-cc MPI-Corrbench/micro-benches/0-level/correct/pt2pt/sendrecv.c -o /tmp/cb11 && mpirun -np 2 /tmp/cb11
```

Expected: Errors detected on all buggy programs, clean output on correct programs.

---

## Project Structure

```
├── CMakeLists.txt        # Build system
├── pass.cpp              # LLVM instrumentation pass (compile-time)
├── runtime.cpp           # Runtime detection library
├── runtime.h             # Runtime header
├── mpiasan-cc            # Drop-in compiler wrapper (replaces mpicc)
├── build.sh              # Build script (installs + compiles)
├── run.sh                # Test runner (compiles + runs all 21 tests)
├── README.md             # Setup and usage guide
├── DESIGN.md             # Approach, alternatives, design decisions
├── IMPLEMENTATION.md     # LLVM pass and runtime details
├── EVALUATION.md         # Test results, metrics, baseline comparison
└── tests/                # 21 test programs with seeded MPI bugs
```

## Architecture

```
Source Code (.c)
      |
      v
 clang-17 + MPISanPass.so      <- Compile-time: injects hooks before MPI calls
      |
      v
 Instrumented Binary + RT      <- Binary calls __mpiasan_send(), __mpiasan_recv(), etc.
      |
      v
 mpirun -np N                  <- Runtime: hooks validate types, buffers, ordering, deadlocks
 libmpiasan_rt.so                 using a shadow communicator for tool-internal MPI traffic
```
