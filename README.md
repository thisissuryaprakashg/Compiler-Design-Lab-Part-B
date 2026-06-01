# Compiler-Integrated MPI Usage Sanitizer

An LLVM IR instrumentation pass + runtime library that detects common MPI usage errors at runtime:
- **Type mismatches** between buffer types and MPI_Datatype
- **Buffer overlaps** with in-flight non-blocking operations
- **Collective ordering violations** (kind, root, sequence mismatches)
- **Deadlocks** (Send-Send, Recv-Recv, tag-ordering, missing call, N-rank cycles)

---

## Files to Share

```
cd_llvm/
├── CMakeLists.txt          # Build system
├── pass.cpp                # LLVM instrumentation pass
├── runtime.cpp             # Runtime library (detection logic)
├── mpiasan-cc              # Compiler wrapper script
├── README.md               # This file
└── tests/                  # 21 test programs with seeded bugs
    ├── test_01_type_mismatch.c
    ├── test_05_buffer_overlap.c
    ├── test_06_collective_order.c
    ├── test_07_deadlock.c
    ├── test_08_deadlock_correct.c      (negative test)
    ├── test_09_deadlock_circular.c
    ├── test_10_deadlock_small.c
    ├── test_11_type_double_as_float.c
    ├── test_12_type_correct.c          (negative test)
    ├── test_13_overlap_partial.c
    ├── test_14_overlap_safe.c          (negative test)
    ├── test_15_collective_root.c
    ├── test_16_deadlock_4rank.c
    ├── test_17_ring_correct.c          (negative test)
    ├── test_18_coll_extra.c
    ├── test_19_coll_reduce_allreduce.c
    ├── test_20_coll_correct_seq.c      (negative test)
    ├── test_21_coll_conditional.c
    ├── test_22_deadlock_halo.c
    ├── test_23_combo_scatter.c
    └── test_24_combo_type_overlap.c
```

---

## Setup Instructions (WSL / Ubuntu)

### Step 1: Install Dependencies

```bash
sudo apt update
sudo apt install -y clang-17 llvm-17-dev cmake make openmpi-bin libopenmpi-dev
```

### Step 2: Copy Project Files

Place all the files listed above into a folder, e.g. `~/cd_llvm/`.

### Step 3: Build the Pass and Runtime

```bash
cd ~/cd_llvm
mkdir build && cd build
cmake ..
make -j4
```

This produces:
- `build/MPISanPass.so` — the LLVM instrumentation pass
- `build/libmpiasan_rt.so` — the runtime library

### Step 4: Make the Wrapper Executable

```bash
chmod +x ~/cd_llvm/mpiasan-cc
```

---

## How to Use

### Compile a test WITH the sanitizer:
```bash
cd ~/cd_llvm
./mpiasan-cc tests/test_01_type_mismatch.c -o /tmp/test_01
mpirun -np 2 /tmp/test_01
```

### Compile the SAME test WITHOUT the sanitizer (for comparison):
```bash
mpicc tests/test_01_type_mismatch.c -o /tmp/test_01_normal
mpirun -np 2 /tmp/test_01_normal
```

**Without sanitizer**: runs silently, bug goes undetected.
**With sanitizer**: catches the bug with a clear error message.

---

## Running All Tests

```bash
cd ~/cd_llvm

# === TYPE MISMATCH ===
./mpiasan-cc tests/test_01_type_mismatch.c -o /tmp/t01 && mpirun -np 2 /tmp/t01
./mpiasan-cc tests/test_11_type_double_as_float.c -o /tmp/t11 && mpirun -np 2 /tmp/t11
./mpiasan-cc tests/test_12_type_correct.c -o /tmp/t12 && mpirun -np 2 /tmp/t12        # No error expected

# === BUFFER OVERLAP ===
./mpiasan-cc tests/test_05_buffer_overlap.c -o /tmp/t05 && mpirun -np 2 /tmp/t05
./mpiasan-cc tests/test_13_overlap_partial.c -o /tmp/t13 && mpirun -np 2 /tmp/t13
./mpiasan-cc tests/test_14_overlap_safe.c -o /tmp/t14 && mpirun -np 2 /tmp/t14        # No error expected

# === COLLECTIVE ORDER ===
./mpiasan-cc tests/test_06_collective_order.c -o /tmp/t06 && timeout 10 mpirun -np 2 /tmp/t06
./mpiasan-cc tests/test_15_collective_root.c -o /tmp/t15 && timeout 10 mpirun -np 2 /tmp/t15
./mpiasan-cc tests/test_18_coll_extra.c -o /tmp/t18 && timeout 10 mpirun -np 2 /tmp/t18
./mpiasan-cc tests/test_19_coll_reduce_allreduce.c -o /tmp/t19 && timeout 10 mpirun -np 2 /tmp/t19
./mpiasan-cc tests/test_20_coll_correct_seq.c -o /tmp/t20 && mpirun -np 2 /tmp/t20    # No error expected
./mpiasan-cc tests/test_21_coll_conditional.c -o /tmp/t21 && timeout 10 mpirun -np 2 /tmp/t21

# === DEADLOCK ===
./mpiasan-cc tests/test_07_deadlock.c -o /tmp/t07 && timeout 10 mpirun -np 2 /tmp/t07
./mpiasan-cc tests/test_08_deadlock_correct.c -o /tmp/t08 && mpirun -np 2 /tmp/t08    # No error expected
./mpiasan-cc tests/test_09_deadlock_circular.c -o /tmp/t09 && timeout 10 mpirun -np 3 /tmp/t09
./mpiasan-cc tests/test_10_deadlock_small.c -o /tmp/t10 && timeout 10 mpirun -np 2 /tmp/t10
./mpiasan-cc tests/test_16_deadlock_4rank.c -o /tmp/t16 && timeout 10 mpirun -np 4 /tmp/t16
./mpiasan-cc tests/test_17_ring_correct.c -o /tmp/t17 && mpirun -np 4 /tmp/t17        # No error expected
./mpiasan-cc tests/test_22_deadlock_halo.c -o /tmp/t22 && timeout 15 mpirun -np 4 /tmp/t22

# === MULTI-ERROR COMBOS ===
./mpiasan-cc tests/test_23_combo_scatter.c -o /tmp/t23 && timeout 10 mpirun -np 2 /tmp/t23
./mpiasan-cc tests/test_24_combo_type_overlap.c -o /tmp/t24 && mpirun -np 2 /tmp/t24
```

---

## CoMD Evaluation (ECP Proxy App)

```bash
cd ~/cd_llvm
git clone https://github.com/ECP-copa/CoMD.git
cd CoMD/src-mpi
cp Makefile.vanilla Makefile

# Build baseline (normal mpicc)
make CC=mpicc OPTFLAGS='-g -O2' -j4
cd .. && time mpirun -np 4 --oversubscribe ./bin/CoMD-mpi -e -x 10 -y 10 -z 10 -i 2 -j 2 -k 1 -n 5

# Build with sanitizer
cd src-mpi && make clean
make CC=~/cd_llvm/mpiasan-cc OPTFLAGS='-g -O2' -j4
cd .. && time mpirun -np 4 --oversubscribe ./bin/CoMD-mpi -e -x 10 -y 10 -z 10 -i 2 -j 2 -k 1 -n 5
```

**Expected**: Zero false positives, negligible overhead.

---

## Architecture

```
Source Code (.c)
      │
      ▼
┌─────────────────┐
│  clang-17 +     │    Compile-time: LLVM pass injects hooks
│  MPISanPass.so  │    before every MPI_Send/Recv/Bcast/etc.
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Instrumented   │    Binary has calls to __mpiasan_send(),
│  Binary + RT    │    __mpiasan_recv(), __mpiasan_collective()
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  mpirun -np N   │    Runtime: hooks validate types, buffers,
│  libmpiasan_rt  │    ordering, and deadlocks using shadow comm
└─────────────────┘
```
