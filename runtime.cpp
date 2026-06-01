// runtime.cpp — MPI Sanitizer Runtime Library
// Implements all __mpiasan_* hooks called by the instrumented pass.
// Detects: type mismatches, buffer overlaps, collective ordering, deadlocks.

#include <mpi.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <set>

// ═══════════════════════════════════════════════════════════════════
// SECTION 1 — Helpers
// ═══════════════════════════════════════════════════════════════════

static const char* mpiTypeName(MPI_Datatype t) {
    if (t == MPI_INT)          return "MPI_INT";
    if (t == MPI_FLOAT)        return "MPI_FLOAT";
    if (t == MPI_DOUBLE)       return "MPI_DOUBLE";
    if (t == MPI_CHAR)         return "MPI_CHAR";
    if (t == MPI_LONG)         return "MPI_LONG";
    if (t == MPI_UNSIGNED)     return "MPI_UNSIGNED";
    if (t == MPI_LONG_LONG)    return "MPI_LONG_LONG";
    if (t == MPI_SHORT)        return "MPI_SHORT";
    if (t == MPI_LONG_DOUBLE)  return "MPI_LONG_DOUBLE";
    if (t == MPI_BYTE)         return "MPI_BYTE";
    return "MPI_UNKNOWN";
}

static size_t mpiTypeSize(MPI_Datatype t) {
    int sz = 0;
    MPI_Type_size(t, &sz);
    return (size_t)(sz > 0 ? sz : 1);
}

static const char* compileTypeName(int id) {
    switch(id) {
        case 1: return "float";
        case 2: return "double";
        case 3: return "int";
        case 4: return "char";
        case 5: return "long/int64";
        case 6: return "short";
        default: return "unknown";
    }
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 2 — Global state & error reporting
// ═══════════════════════════════════════════════════════════════════

static int        g_rank = -1;
static int        g_worldSize = 1;
static MPI_Comm   g_shadowComm = MPI_COMM_NULL; // dedicated comm for tool's own MPI calls
static std::mutex g_mutex;
static int        g_errorCount = 0;

static void ensureInit() {
    if (g_rank >= 0) return;
    int init = 0;
    MPI_Initialized(&init);
    if (!init) return;
    MPI_Comm_rank(MPI_COMM_WORLD, &g_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &g_worldSize);
    // Create a shadow communicator so our tool's MPI calls never
    // interfere with the application's communication.
    MPI_Comm_dup(MPI_COMM_WORLD, &g_shadowComm);
}

static void reportError(const char* category, const char* callsite, const char* detail) {
    ensureInit();
    g_errorCount++;
    fprintf(stderr,
        "\n==MPISanitizer== ERROR: [%s] on Rank %d\n"
        "  Call site : %s\n"
        "  Detail    : %s\n",
        category, g_rank, callsite ? callsite : "unknown", detail);
    fflush(stderr);
}

static void reportWarning(const char* category, const char* callsite, const char* detail) {
    ensureInit();
    fprintf(stderr,
        "==MPISanitizer== WARNING: [%s] on Rank %d\n"
        "  Call site : %s\n"
        "  Detail    : %s\n",
        category, g_rank, callsite ? callsite : "unknown", detail);
    fflush(stderr);
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 3 — Type Mismatch Detection (LOCAL)
//
// Compare compile-time buffer type (extracted by the LLVM pass)
// against the MPI_Datatype argument in the SAME call, on the SAME rank.
// No cross-rank communication needed.
// ═══════════════════════════════════════════════════════════════════

// Returns what compile_type_id the given MPI_Datatype maps to (or 0 if unknown)
static int mpiDatatypeToTypeID(MPI_Datatype t) {
    if (t == MPI_FLOAT)                                  return 1;
    if (t == MPI_DOUBLE)                                 return 2;
    if (t == MPI_INT || t == MPI_UNSIGNED)                return 3;
    if (t == MPI_CHAR || t == MPI_BYTE
        || t == MPI_UNSIGNED_CHAR)                       return 4;
    if (t == MPI_LONG || t == MPI_LONG_LONG
        || t == MPI_UNSIGNED_LONG_LONG)                  return 5;
    if (t == MPI_SHORT)                                  return 6;
    return 0;
}

static void localTypeCheck(int compile_type_id, MPI_Datatype mpi_type,
                            const char* call_kind, const char* site) {
    if (compile_type_id == 0) return; // pass couldn't determine type
    int expected = mpiDatatypeToTypeID(mpi_type);
    if (expected == 0) return; // unknown MPI datatype (derived etc.)

    if (compile_type_id != expected) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "%s: buffer is '%s*' at compile time but MPI_Datatype is %s",
            call_kind, compileTypeName(compile_type_id), mpiTypeName(mpi_type));
        reportError("TYPE_MISMATCH", site, msg);
    }
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 4 — Cross-Rank Type Mismatch (via shadow communicator)
//
// On send: piggyback the MPI_Datatype to the receiver via shadow comm.
// On recv: receive the piggybacked type and compare.
// ═══════════════════════════════════════════════════════════════════

// We use tag offset 30000+ on shadow comm to avoid clashing
static const int SHADOW_TAG_BASE = 30000;

static void crossRankTypeCheck_Send(MPI_Datatype mpi_type, int dest, int tag, const char* site) {
    if (g_shadowComm == MPI_COMM_NULL || dest < 0 || dest >= g_worldSize) return;
    static int type_id_buf;  // static so it survives past Isend
    type_id_buf = mpiDatatypeToTypeID(mpi_type);
    int shadow_tag = SHADOW_TAG_BASE + (tag % 10000);
    MPI_Request req;
    MPI_Isend(&type_id_buf, 1, MPI_INT, dest, shadow_tag, g_shadowComm, &req);
    MPI_Request_free(&req);  // fire and forget — don't block
}

static void crossRankTypeCheck_Recv(MPI_Datatype mpi_type, int src, int tag, const char* site) {
    if (g_shadowComm == MPI_COMM_NULL || src < 0 || src >= g_worldSize) return;
    int shadow_tag = SHADOW_TAG_BASE + (tag % 10000);

    // Non-blocking probe — only check if the shadow message has arrived
    int flag = 0;
    MPI_Status status;
    MPI_Iprobe(src, shadow_tag, g_shadowComm, &flag, &status);
    if (!flag) return;  // shadow message not yet available, skip

    int remote_type_id = 0;
    MPI_Recv(&remote_type_id, 1, MPI_INT, src, shadow_tag, g_shadowComm, MPI_STATUS_IGNORE);

    int local_type_id = mpiDatatypeToTypeID(mpi_type);
    if (remote_type_id != 0 && local_type_id != 0 && remote_type_id != local_type_id) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "Sender (rank %d) used %s but this Recv uses %s (tag=%d)",
            src, compileTypeName(remote_type_id), mpiTypeName(mpi_type), tag);
        reportError("TYPE_MISMATCH", site, msg);
    }
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 5 — Buffer Overlap Detection
//
// Track in-flight buffers from non-blocking sends/recvs.
// On each new MPI call, check if the buffer overlaps any active one.
// On MPI_Wait, release the tracked buffer.
// ═══════════════════════════════════════════════════════════════════

struct BufferRange {
    uintptr_t start;
    uintptr_t end;        // exclusive
    std::string site;     // where it was registered
};

static std::unordered_map<uintptr_t, BufferRange> g_activeBuffers;

static bool rangesOverlap(uintptr_t s1, uintptr_t e1, uintptr_t s2, uintptr_t e2) {
    return s1 < e2 && s2 < e1;
}

static void checkBufferOverlap(void* buf, int count, MPI_Datatype mpi_type, const char* site) {
    if (!buf || count <= 0) return;
    uintptr_t bstart = (uintptr_t)buf;
    uintptr_t bend   = bstart + (uintptr_t)count * mpiTypeSize(mpi_type);
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto& kv : g_activeBuffers) {
        if (rangesOverlap(bstart, bend, kv.second.start, kv.second.end)) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                "Buffer [%p, +%zu bytes] overlaps with in-flight buffer from %s",
                buf, (size_t)count * mpiTypeSize(mpi_type),
                kv.second.site.c_str());
            reportError("BUFFER_OVERLAP", site, msg);
        }
    }
}

// Register a buffer as in-flight (called after Isend/Irecv to track the request)
static void trackNonBlockingBuffer(void* buf, int count, MPI_Datatype mpi_type,
                                    const char* site) {
    if (!buf || count <= 0) return;
    uintptr_t bstart = (uintptr_t)buf;
    uintptr_t bend   = bstart + (uintptr_t)count * mpiTypeSize(mpi_type);
    std::lock_guard<std::mutex> lk(g_mutex);
    // Key on buffer start address (approximation; real key should be MPI_Request)
    g_activeBuffers[bstart] = {bstart, bend, site ? site : "unknown"};
}

static void releaseBuffer(void* request_or_buf) {
    if (!request_or_buf) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    // Try to release by buffer start address
    g_activeBuffers.erase((uintptr_t)request_or_buf);
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 6 — Collective Ordering Detection
//
// Each rank keeps a sequence counter of collectives.
// At each collective, we use Allgather on the shadow communicator
// to exchange (seq, kind, root) and detect ordering mismatches.
// ═══════════════════════════════════════════════════════════════════

static int g_collSeq = 0;

static const char* collectiveKindName(int k) {
    switch(k) {
        case 4:  return "MPI_Bcast";
        case 5:  return "MPI_Scatter";
        case 6:  return "MPI_Gather";
        case 7:  return "MPI_Allreduce";
        case 8:  return "MPI_Reduce";
        case 9:  return "MPI_Barrier";
        case 10: return "MPI_Alltoall";
        default: return "MPI_Collective";
    }
}

static bool g_collBroken = false;  // once set, skip all further collective checks

static void checkCollectiveOrder(int kind, int root, const char* site) {
    ensureInit();
    if (g_shadowComm == MPI_COMM_NULL || g_collBroken) return;

    // Pack local info: [seq, kind, root]
    int localInfo[3] = { g_collSeq, kind, root };
    std::vector<int> allInfo(g_worldSize * 3);

    MPI_Allgather(localInfo, 3, MPI_INT,
                   allInfo.data(), 3, MPI_INT, g_shadowComm);

    for (int r = 0; r < g_worldSize; r++) {
        if (r == g_rank) continue;
        int rSeq  = allInfo[r * 3 + 0];
        int rKind = allInfo[r * 3 + 1];
        int rRoot = allInfo[r * 3 + 2];

        // Check sequence count mismatch (rank skipped or has extra collectives)
        if (rSeq != g_collSeq) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                "Rank %d has done %d collectives, but rank %d has done %d",
                g_rank, g_collSeq, r, rSeq);
            reportError("COLLECTIVE_ORDER", site, msg);
            g_collBroken = true;
            break;
        }
        // Check kind mismatch (e.g., rank 0 calls Bcast but rank 1 calls Barrier)
        if (rKind != kind) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                "Rank %d calls %s but rank %d calls %s at collective #%d",
                g_rank, collectiveKindName(kind), r, collectiveKindName(rKind), g_collSeq);
            reportError("COLLECTIVE_ORDER", site, msg);
            g_collBroken = true;
            break;
        }
        // Check root mismatch (only for rooted collectives)
        if (root >= 0 && rRoot >= 0 && rRoot != root) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                "Rank %d specifies root=%d but rank %d specifies root=%d for %s",
                g_rank, root, r, rRoot, collectiveKindName(kind));
            reportError("COLLECTIVE_ORDER", site, msg);
            g_collBroken = true;
            break;
        }
    }
    g_collSeq++;
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 7 — Deadlock Detection
//
// Two approaches:
// A) Pairwise: at each blocking send, use non-blocking Isend+Irecv
//    on shadow comm to check if the peer is also sending to us.
// B) Global (at barriers): gather all pending peers and DFS for cycles.
// ═══════════════════════════════════════════════════════════════════

static int         g_pendingPeer = -1;
static std::string g_pendingSite;
static const int   DEADLOCK_TAG = 29999;

static void registerBlockingCall(int peer, const char* site) {
    g_pendingPeer = peer;
    g_pendingSite = site ? site : "unknown";
}

static void clearBlockingCall() {
    g_pendingPeer = -1;
    g_pendingSite = "";
}

// Called from BOTH send and recv hooks so all ranks participate.
// Uses Allgather to exchange [peer, is_send, tag] across all ranks,
// then builds a wait-for graph of UNSATISFIED operations to detect cycles.
// Handles Send-Send, Recv-Recv, mixed, and tag-ordering deadlocks.
static void checkP2PDeadlock(int peer, int is_send, int tag, const char* site) {
    ensureInit();
    if (g_shadowComm == MPI_COMM_NULL) return;

    // Exchange [peer, is_send, tag] with all ranks
    int info[3] = { peer, is_send, tag };
    std::vector<int> allInfo(g_worldSize * 3);
    MPI_Allgather(info, 3, MPI_INT, allInfo.data(), 3, MPI_INT, g_shadowComm);

    // Build wait-for graph with only UNSATISFIED edges.
    // An operation is satisfied if the peer has the matching counterpart:
    //   Send(r→p, tag_r) satisfied if p does Recv(from r, tag_p) AND tags match
    //   Recv(r from p, tag_r) satisfied if p does Send(to r, tag_p) AND tags match
    std::vector<int> waitFor(g_worldSize, -1);
    for (int r = 0; r < g_worldSize; r++) {
        int r_peer    = allInfo[r * 3 + 0];
        int r_is_send = allInfo[r * 3 + 1];
        int r_tag     = allInfo[r * 3 + 2];
        if (r_peer < 0 || r_peer >= g_worldSize) continue;

        int p_peer    = allInfo[r_peer * 3 + 0];
        int p_is_send = allInfo[r_peer * 3 + 1];
        int p_tag     = allInfo[r_peer * 3 + 2];

        bool satisfied = false;
        if (r_is_send) {
            // r sends to peer. Satisfied if peer recvs from r with matching tag.
            bool tags_ok = (p_tag == r_tag || p_tag == MPI_ANY_TAG);
            satisfied = (!p_is_send && p_peer == r && tags_ok);
        } else {
            // r recvs from peer. Satisfied if peer sends to r with matching tag.
            bool tags_ok = (p_tag == r_tag || r_tag == MPI_ANY_TAG);
            satisfied = (p_is_send && p_peer == r && tags_ok);
        }
        if (!satisfied) {
            waitFor[r] = r_peer;  // this rank is stuck
        }
    }

    // Only report from ranks that are stuck (have an unsatisfied edge)
    if (waitFor[g_rank] < 0) return;

    // DFS from this rank to find cycles
    std::set<int> visited;
    std::vector<int> path;
    int cur = g_rank;
    while (cur >= 0 && cur < g_worldSize && waitFor[cur] >= 0 && !visited.count(cur)) {
        path.push_back(cur);
        visited.insert(cur);
        cur = waitFor[cur];
    }
    if (cur >= 0 && visited.count(cur)) {
        std::string cycle;
        bool in_cycle = false;
        for (int r : path) {
            if (r == cur) in_cycle = true;
            if (in_cycle) cycle += std::to_string(r) + " -> ";
        }
        cycle += std::to_string(cur);
        char msg[512];
        snprintf(msg, sizeof(msg), "Deadlock cycle detected: %s", cycle.c_str());
        reportError("DEADLOCK", site, msg);
        return;
    }

    // Check for dangling deadlocks: this rank is stuck but target is idle
    // (e.g., rank 1 does Recv from rank 0, but rank 0 is at Finalize)
    int target = waitFor[g_rank];
    if (target >= 0 && target < g_worldSize) {
        int t_peer = allInfo[target * 3 + 0];
        if (t_peer < 0) {
            // target rank is idle (at Finalize, no pending MPI operation)
            char msg[512];
            snprintf(msg, sizeof(msg),
                "Rank %d is blocked waiting for Rank %d, "
                "but Rank %d has no matching MPI call (missing Send/Recv)",
                g_rank, target, target);
            reportError("DEADLOCK", site, msg);
        }
    }
}

// Global check (called at barriers): gather all pending peers, DFS for cycles
static void checkDeadlock(const char* site) {
    ensureInit();
    if (g_shadowComm == MPI_COMM_NULL) return;

    std::vector<int> allPeers(g_worldSize, -1);
    MPI_Allgather(&g_pendingPeer, 1, MPI_INT,
                   allPeers.data(), 1, MPI_INT, g_shadowComm);

    for (int start = 0; start < g_worldSize; start++) {
        if (allPeers[start] < 0 || allPeers[start] >= g_worldSize) continue;

        std::vector<int> path;
        std::set<int> visited;
        int cur = start;
        while (cur >= 0 && cur < g_worldSize && !visited.count(cur)) {
            path.push_back(cur);
            visited.insert(cur);
            cur = allPeers[cur];
        }
        if (cur >= 0 && visited.count(cur)) {
            std::string cycle;
            bool in_cycle = false;
            for (int r : path) {
                if (r == cur) in_cycle = true;
                if (in_cycle) cycle += std::to_string(r) + " -> ";
            }
            cycle += std::to_string(cur);
            char msg[512];
            snprintf(msg, sizeof(msg), "Deadlock cycle detected: %s", cycle.c_str());
            reportError("DEADLOCK", site, msg);
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 8 — Public Hooks (extern "C", called from instrumented IR)
// ═══════════════════════════════════════════════════════════════════

extern "C" {

// Called BEFORE MPI_Send / MPI_Isend
void __mpiasan_send(void* buf, int count, MPI_Datatype t,
                    int compile_type_id, int dest, int tag, const char* site) {
    ensureInit();
    // Check 1: local type mismatch (buffer type vs MPI_Datatype)
    localTypeCheck(compile_type_id, t, "MPI_Send", site);
    // Check 2: buffer overlap with in-flight non-blocking buffers
    checkBufferOverlap(buf, count, t, site);
    // Check 3: cross-rank type mismatch (piggyback on shadow comm)
    crossRankTypeCheck_Send(t, dest, tag, site);
    // Check 4: pairwise deadlock (is peer also sending to us?)
    checkP2PDeadlock(dest, 1, tag, site);
    // Track for deadlock detection
    registerBlockingCall(dest, site);
}

// Called BEFORE MPI_Recv / MPI_Irecv
void __mpiasan_recv(void* buf, int count, MPI_Datatype t,
                    int compile_type_id, int src, int tag, const char* site) {
    ensureInit();
    // Check 1: local type mismatch
    localTypeCheck(compile_type_id, t, "MPI_Recv", site);
    // Check 2: buffer overlap
    checkBufferOverlap(buf, count, t, site);
    // Check 3: cross-rank type mismatch
    crossRankTypeCheck_Recv(t, src, tag, site);
    // Check 4: pairwise deadlock (participate in peer's Sendrecv)
    checkP2PDeadlock(src, 0, tag, site);
    // After this call completes, the blocking call is done
    clearBlockingCall();
}

// Called BEFORE collectives (Bcast, Scatter, Gather, Allreduce, Reduce, Alltoall)
void __mpiasan_collective(int kind, void* buf, int count,
                           MPI_Datatype t, int compile_type_id,
                           int root, const char* site) {
    ensureInit();
    if (g_shadowComm == MPI_COMM_NULL) return; // MPI not initialized yet

    // Check 1: local type mismatch (buffer type vs MPI_Datatype)
    if (buf && count > 0) {
        localTypeCheck(compile_type_id, t, collectiveKindName(kind), site);
    }
    // Check 2: buffer overlap check
    if (buf && count > 0) {
        checkBufferOverlap(buf, count, t, site);
    }
    // Check 3: collective ordering, kind mismatch, root mismatch across ranks
    checkCollectiveOrder(kind, root, site);
    clearBlockingCall();
}

// Called BEFORE MPI_Barrier
void __mpiasan_barrier(const char* site) {
    ensureInit();
    if (g_shadowComm == MPI_COMM_NULL) return;

    // Barrier is a collective — check ordering
    checkCollectiveOrder(9 /* Barrier */, -1, site);
    // Also check for deadlock at this synchronization point
    checkDeadlock(site);
    clearBlockingCall();
}

// Called BEFORE MPI_Wait / MPI_Waitall
void __mpiasan_wait(void* request, const char* site) {
    ensureInit();
    releaseBuffer(request);
    clearBlockingCall();
}

// Called AFTER MPI_Isend / MPI_Irecv to track the buffer as in-flight
void __mpiasan_track_buffer(void* buf, int count, MPI_Datatype t, const char* site) {
    ensureInit();
    trackNonBlockingBuffer(buf, count, t, site);
}

// Lightweight recv-side type check for Gather/Scatter
void __mpiasan_check_recv_type(int compile_type_id, MPI_Datatype t, const char* site) {
    ensureInit();
    if (compile_type_id > 0) {
        localTypeCheck(compile_type_id, t, "MPI_Gather/Scatter(recv)", site);
    }
}

// Called BEFORE MPI_Finalize — participate in any pending P2P Allgather as idle
void __mpiasan_finalize(const char* site) {
    ensureInit();
    if (g_shadowComm == MPI_COMM_NULL) return;
    // Participate as idle (peer=-1) so stuck ranks can detect missing calls
    checkP2PDeadlock(-1, 0, 0, site);
}

} // extern "C"
