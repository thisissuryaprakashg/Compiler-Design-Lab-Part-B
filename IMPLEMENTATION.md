# Implementation Details

## LLVM Pass (`pass.cpp`)

The pass is implemented as an LLVM **NewPassManager module pass** registered via `PassPluginLibraryInfo`. It is loaded into clang-17 using `-fpass-plugin=MPISanPass.so`.

### MPI Call Classification

The pass classifies MPI functions into categories using string matching on the callee name:

- `MPI_Send`, `MPI_Isend` → Send/Isend
- `MPI_Recv`, `MPI_Irecv` → Recv/Irecv
- `MPI_Bcast`, `MPI_Reduce`, `MPI_Gather`, `MPI_Scatter`, `MPI_Allreduce`, `MPI_Alltoall` → Collective
- `MPI_Barrier` → Barrier
- `MPI_Wait`, `MPI_Waitall` → Wait
- `MPI_Init`, `MPI_Finalize` → Init/Finalize

### Compile-Time Type Extraction

For each buffer argument, the pass inspects the LLVM IR type to determine the original C type:

```
float*  → Type ID 1
double* → Type ID 2
int*    → Type ID 3
char*   → Type ID 4
long*   → Type ID 5
short*  → Type ID 6
```

This works by walking through `BitCastInst`, `GetElementPtrInst`, and `AllocaInst` chains to find the underlying allocated type. This information is unavailable to PMPI-based tools which only see `void*`.

### Hook Injection

For each MPI call site, the pass inserts a call to the corresponding runtime hook BEFORE the MPI function:

- `MPI_Send` → `__mpiasan_send(buf, count, type, compile_type_id, dest, tag, callsite)`
- `MPI_Recv` → `__mpiasan_recv(buf, count, type, compile_type_id, src, tag, callsite)`
- `MPI_Bcast/Reduce/Gather/...` → `__mpiasan_collective(kind, buf, count, type, compile_type_id, root, callsite)`
- `MPI_Barrier` → `__mpiasan_barrier(callsite)`
- `MPI_Wait` → `__mpiasan_wait(request, callsite)`
- `MPI_Finalize` → `__mpiasan_finalize(callsite)`

For `MPI_Isend`, an additional hook `__mpiasan_track_buffer` is inserted AFTER the call to register the buffer as in-flight.

The `callsite` argument is a global string constant containing the source filename and line number, extracted using LLVM debug metadata (`DILocation`).

---

## Runtime Library (`runtime.cpp`)

### Initialization

On first hook invocation, `ensureInit()`:
1. Queries `MPI_Comm_rank` and `MPI_Comm_size`
2. Creates a shadow communicator via `MPI_Comm_dup(MPI_COMM_WORLD)`

All internal tool communication uses the shadow communicator to avoid interfering with the application.

### Detection 1: Type Mismatch (`localTypeCheck`)

Compares the compile-time type size against `MPI_Type_size` of the declared MPI_Datatype:
- `float*` (4 bytes) + `MPI_INT` (4 bytes) → sizes match but type names differ → **warning**
- `float*` (4 bytes) + `MPI_DOUBLE` (8 bytes) → sizes differ → **error**
- `int*` (4 bytes) + `MPI_INT` (4 bytes) → match → **pass**

### Detection 2: Buffer Overlap (`checkBufferOverlap`)

Maintains a list of `{address, size}` for in-flight `MPI_Isend` buffers. When a new send/recv is issued, checks if the new buffer range `[buf, buf + count * type_size)` overlaps with any tracked range. Buffers are released when `MPI_Wait` is called.

### Detection 3: Collective Ordering (`checkCollectiveOrder`)

Each rank maintains a local collective sequence counter. On each collective call:
1. `MPI_Allgather` exchanges `[sequence, kind, root]` across all ranks on the shadow communicator
2. If any rank's sequence number differs → ordering violation
3. If any rank's collective kind differs → kind mismatch (e.g., rank 0 calls Barrier, rank 1 calls Bcast)
4. If any rank's root differs → root mismatch

### Detection 4: Deadlock (`checkP2PDeadlock`)

Called from both send and recv hooks so all ranks participate:
1. `MPI_Allgather` exchanges `[peer, is_send, tag]` across all ranks
2. Builds a **wait-for graph** with only **unsatisfied** edges:
   - `Send(r→p)` is satisfied if rank p does `Recv(from r)` with matching tag
   - `Recv(r from p)` is satisfied if rank p does `Send(to r)` with matching tag
3. **Cycle detection:** DFS from each rank — a cycle means circular deadlock (e.g., `0→1→2→0`)
4. **Dangling edge detection:** If a rank is stuck but its target is idle (at Finalize), the target will never provide the matching operation — missing-call deadlock

### Error Reporting

All errors are printed to stderr in a consistent format:
```
==MPISanitizer== ERROR: [CATEGORY] on Rank N
  Call site : filename.c:LINE
  Detail    : Human-readable description
```
