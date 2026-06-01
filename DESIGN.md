# Design Document

## Problem

MPI programs can contain subtle correctness errors — type mismatches, buffer aliasing, collective ordering violations, and deadlocks — that compilers and the MPI runtime do not detect. These bugs cause silent data corruption or indefinite hangs, and are extremely difficult to diagnose in distributed programs.

## Approach: Compiler-Integrated Instrumentation

We chose an LLVM-integrated approach over existing alternatives for three key reasons:

1. **Access to compile-time type information.** By instrumenting at the IR level, the pass can inspect the C-level type of buffer pointers (e.g., `float*` vs `double*`). External tools like MUST only see raw `void*` pointers at runtime and cannot perform this check.

2. **Zero source modification.** The wrapper script (`mpiasan-cc`) is a drop-in replacement for `mpicc`. Users compile with `./mpiasan-cc` instead of `mpicc` — no pragmas, annotations, or code changes needed.

3. **Shadow communicator isolation.** All tool-internal MPI communication (Allgather for deadlock detection, type checking, etc.) happens on a duplicated communicator, ensuring the sanitizer never interferes with the application's own MPI traffic.

## Alternatives Considered

**MUST (RWTH Aachen):** The most mature MPI correctness tool. It uses PMPI wrappers (runtime interposition) and is Java-based. Pros: extensive coverage, handles complex patterns. Cons: standalone tool with heavyweight setup, cannot access compile-time type info, significant overhead.

**MPI_T / PMPI Wrappers:** Standard MPI profiling interface. Pros: portable, no compiler dependency. Cons: only sees runtime arguments (no type info), requires manual instrumentation of each MPI function, cannot inspect buffer types at the source level.

**Static Analysis (e.g., MPI-Checker in Clang Static Analyzer):** Analyzes code without running it. Pros: catches bugs before execution. Cons: high false positive rate, cannot reason about runtime values (rank, dynamic control flow), misses data-dependent bugs.

**Our Approach (LLVM Pass + Runtime):** Combines compile-time type extraction with runtime validation. The pass runs at `-O0` through `-O2` and injects lightweight hooks. The runtime performs cross-rank validation using a shadow communicator. This gives us type-aware checking that static analysis and PMPI wrappers cannot provide, with lower overhead than MUST.

## Design Decisions

- **Hook placement:** Hooks are inserted BEFORE each MPI call so the sanitizer can validate arguments before they enter MPI. For non-blocking operations (MPI_Isend), an additional hook is inserted AFTER to track the buffer as in-flight.

- **Shadow communicator:** Created via `MPI_Comm_dup(MPI_COMM_WORLD)` during initialization. All Allgather-based checks use this communicator. This prevents the sanitizer's internal traffic from matching the application's receives or corrupting its message ordering.

- **Wait-for graph deadlock detection:** Instead of pairwise checking (which misses N-rank cycles), each rank broadcasts its current state `[peer, is_send, tag]` via Allgather. The runtime builds a wait-for graph of unsatisfied operations and uses DFS to detect both cycles (circular deadlocks) and dangling edges (missing-call deadlocks).

- **Finalize hook:** Ranks that reach `MPI_Finalize` without calling any MPI operation participate as "idle" in the deadlock check Allgather. This enables detection of missing-call deadlocks where one rank never posts a matching Send/Recv.
