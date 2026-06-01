#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// ── MPI functions we care about and how to classify them ──────────
enum class MPIKind {
    Send, Recv, Isend, Irecv,
    Bcast, Scatter, Gather, Allreduce, Reduce, Barrier, Alltoall,
    Wait, Waitall,
    Init, Finalize,
    Other
};

static MPIKind classifyMPI(StringRef name) {
    if (name == "MPI_Send"  || name == "PMPI_Send")   return MPIKind::Send;
    if (name == "MPI_Recv"  || name == "PMPI_Recv")   return MPIKind::Recv;
    if (name == "MPI_Isend" || name == "PMPI_Isend")  return MPIKind::Isend;
    if (name == "MPI_Irecv" || name == "PMPI_Irecv")  return MPIKind::Irecv;
    if (name == "MPI_Bcast" || name == "PMPI_Bcast")  return MPIKind::Bcast;
    if (name == "MPI_Scatter"|| name=="PMPI_Scatter")  return MPIKind::Scatter;
    if (name == "MPI_Gather" || name=="PMPI_Gather")   return MPIKind::Gather;
    if (name == "MPI_Allreduce"||name=="PMPI_Allreduce") return MPIKind::Allreduce;
    if (name == "MPI_Reduce" || name=="PMPI_Reduce")   return MPIKind::Reduce;
    if (name == "MPI_Barrier"|| name=="PMPI_Barrier")  return MPIKind::Barrier;
    if (name == "MPI_Alltoall"||name=="PMPI_Alltoall") return MPIKind::Alltoall;
    if (name == "MPI_Wait"   || name=="PMPI_Wait")     return MPIKind::Wait;
    if (name == "MPI_Waitall"|| name=="PMPI_Waitall")  return MPIKind::Waitall;
    if (name == "MPI_Init"   || name=="PMPI_Init")     return MPIKind::Init;
    if (name == "MPI_Finalize"||name=="PMPI_Finalize") return MPIKind::Finalize;
    return MPIKind::Other;
}
// ── Helper: get a string describing the source location ───────────
static std::string getCallSite(CallInst *CI) {
    if (DILocation *Loc = CI->getDebugLoc()) {
        return Loc->getFilename().str() + ":" + std::to_string(Loc->getLine());
    }
    return "unknown";
}

// ── Helper: extract compile-time element type of a buffer pointer ─
// Type IDs: 0=unknown, 1=float, 2=double, 3=i32(int), 4=i8(char), 5=i64(long), 6=i16(short)
static int getBufferTypeID(Value *BufArg) {
    // Walk back through GEP / bitcast chains to find the alloca
    Value *cur = BufArg;
    for (int depth = 0; depth < 8; depth++) {
        if (auto *GEP = dyn_cast<GetElementPtrInst>(cur)) {
            cur = GEP->getPointerOperand();
        } else if (auto *BC = dyn_cast<BitCastInst>(cur)) {
            cur = BC->getOperand(0);
        } else if (auto *AI = dyn_cast<AllocaInst>(cur)) {
            Type *T = AI->getAllocatedType();
            // Unwrap array types: float[4] -> float
            while (auto *AT = dyn_cast<ArrayType>(T))
                T = AT->getElementType();
            if (T->isFloatTy())         return 1;
            if (T->isDoubleTy())        return 2;
            if (T->isIntegerTy(32))     return 3;
            if (T->isIntegerTy(8))      return 4;
            if (T->isIntegerTy(64))     return 5;
            if (T->isIntegerTy(16))     return 6;
            return 0;
        } else {
            break;
        }
    }
    return 0; // unknown
}

// ── The Pass itself ───────────────────────────────────────────────
struct MPISanPass : PassInfoMixin<MPISanPass> {

    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        LLVMContext &Ctx = M.getContext();

        // Types we'll use in hook signatures
        Type *VoidTy  = Type::getVoidTy(Ctx);
        Type *I8PtrTy = PointerType::getUnqual(Ctx);
        Type *I32Ty   = Type::getInt32Ty(Ctx);

        // Declare runtime hook signatures
        // __mpiasan_send(buf, count, mpi_type_ptr, compile_type_id, dest, tag, callsite)
        FunctionCallee HookSend = M.getOrInsertFunction(
            "__mpiasan_send",
            FunctionType::get(VoidTy,
                {I8PtrTy, I32Ty, I8PtrTy, I32Ty, I32Ty, I32Ty, I8PtrTy}, false));

        // __mpiasan_recv(buf, count, mpi_type_ptr, compile_type_id, src, tag, callsite)
        FunctionCallee HookRecv = M.getOrInsertFunction(
            "__mpiasan_recv",
            FunctionType::get(VoidTy,
                {I8PtrTy, I32Ty, I8PtrTy, I32Ty, I32Ty, I32Ty, I8PtrTy}, false));

        // __mpiasan_collective(kind, buf, count, mpi_type_ptr, compile_type_id, root, callsite)
        FunctionCallee HookColl = M.getOrInsertFunction(
            "__mpiasan_collective",
            FunctionType::get(VoidTy,
                {I32Ty, I8PtrTy, I32Ty, I8PtrTy, I32Ty, I32Ty, I8PtrTy}, false));

        // __mpiasan_wait(request_ptr, callsite)
        FunctionCallee HookWait = M.getOrInsertFunction(
            "__mpiasan_wait",
            FunctionType::get(VoidTy, {I8PtrTy, I8PtrTy}, false));

        // __mpiasan_barrier(callsite)
        FunctionCallee HookBarrier = M.getOrInsertFunction(
            "__mpiasan_barrier",
            FunctionType::get(VoidTy, {I8PtrTy}, false));

        // __mpiasan_finalize(callsite)
        FunctionCallee HookFinalize = M.getOrInsertFunction(
            "__mpiasan_finalize",
            FunctionType::get(VoidTy, {I8PtrTy}, false));

        // __mpiasan_check_recv_type(compile_type_id, mpi_type_ptr, callsite)
        // Lightweight hook for recv-side type check in Gather/Scatter
        FunctionCallee HookCheckRecvType = M.getOrInsertFunction(
            "__mpiasan_check_recv_type",
            FunctionType::get(VoidTy, {I32Ty, I8PtrTy, I8PtrTy}, false));

        // __mpiasan_track_buffer(buf, count, mpi_type_ptr, callsite)
        // Called AFTER MPI_Isend/MPI_Irecv to track in-flight buffers
        FunctionCallee HookTrackBuf = M.getOrInsertFunction(
            "__mpiasan_track_buffer",
            FunctionType::get(VoidTy, {I8PtrTy, I32Ty, I8PtrTy, I8PtrTy}, false));

        // Walk every function → every BB → every instruction
        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    CallInst *CI = dyn_cast<CallInst>(&I);
                    if (!CI) continue;
                    Function *Callee = CI->getCalledFunction();
                    if (!Callee) continue;

                    StringRef FName = Callee->getName();
                    if (!FName.startswith("MPI_") && !FName.startswith("PMPI_"))
                        continue;

                    MPIKind Kind = classifyMPI(FName);
                    if (Kind == MPIKind::Other ||
                        Kind == MPIKind::Init) continue;

                    IRBuilder<> B(CI); // Insert BEFORE the MPI call

                    // Embed call site as a global string constant
                    std::string CS = getCallSite(CI);
                    Value *CallSiteStr = B.CreateGlobalStringPtr(CS);

                    // args: buf=0, count=1, datatype=2, dest/src=3, tag=4
                    auto getArg = [&](int idx) -> Value* {
                        if ((int)CI->arg_size() > idx)
                            return CI->getArgOperand(idx);
                        return ConstantInt::get(I32Ty, -1);
                    };

                    Value *Buf  = getArg(0);
                    Value *Count= getArg(1);
                    Value *DType= getArg(2);
                    Value *Peer = getArg(3);
                    Value *Tag  = getArg(4);

                    // Cast buf to i8* for the hook
                    Value *BufPtr = B.CreateBitOrPointerCast(Buf, I8PtrTy);

                    // Cast count/datatype/peer/tag to i32
                    auto toI32 = [&](Value *V) -> Value* {
                        if (V->getType() == I32Ty) return V;
                        if (V->getType()->isIntegerTy())
                            return B.CreateTruncOrBitCast(V, I32Ty);
                        return ConstantInt::get(I32Ty, -1);
                    };

                    // Cast MPI_Datatype arg to opaque pointer (it's a pointer in OpenMPI)
                    Value *DTypePtr = B.CreateBitOrPointerCast(DType, I8PtrTy);

                    // Extract compile-time type ID of the buffer (0 = unknown)
                    Value *TypeID = ConstantInt::get(I32Ty, getBufferTypeID(Buf));

                    if (Kind == MPIKind::Send || Kind == MPIKind::Isend) {
                        B.CreateCall(HookSend,
                            {BufPtr, toI32(Count), DTypePtr, TypeID,
                             toI32(Peer), toI32(Tag), CallSiteStr});
                        // For Isend: insert AFTER the call to track the buffer as in-flight
                        if (Kind == MPIKind::Isend) {
                            IRBuilder<> BAfter(CI->getNextNode());
                            BAfter.CreateCall(HookTrackBuf,
                                {BufPtr, toI32(Count), DTypePtr, CallSiteStr});
                        }

                    } else if (Kind == MPIKind::Recv || Kind == MPIKind::Irecv) {
                        B.CreateCall(HookRecv,
                            {BufPtr, toI32(Count), DTypePtr, TypeID,
                             toI32(Peer), toI32(Tag), CallSiteStr});

                    } else if (Kind == MPIKind::Barrier) {
                        B.CreateCall(HookBarrier, {CallSiteStr});

                    } else if (Kind == MPIKind::Finalize) {
                        B.CreateCall(HookFinalize, {CallSiteStr});

                    } else if (Kind == MPIKind::Wait || Kind == MPIKind::Waitall) {
                        Value *ReqPtr = B.CreateBitOrPointerCast(getArg(0), I8PtrTy);
                        B.CreateCall(HookWait, {ReqPtr, CallSiteStr});

                    } else {
                        // Bcast, Scatter, Gather, Allreduce, Reduce, Alltoall
                        Value *KindVal = ConstantInt::get(I32Ty, (int)Kind);
                        Value *Root;
                        if (Kind == MPIKind::Bcast)
                            Root = toI32(getArg(3));        // Bcast(buf,count,type,root,comm)
                        else if (Kind == MPIKind::Reduce)
                            Root = toI32(getArg(5));        // Reduce(sbuf,rbuf,count,type,op,root,comm)
                        else if (Kind == MPIKind::Scatter || Kind == MPIKind::Gather)
                            Root = toI32(getArg(6));        // Scatter/Gather(...,root,comm)
                        else
                            Root = ConstantInt::get(I32Ty, -1); // Allreduce, Alltoall — no root
                        B.CreateCall(HookColl,
                            {KindVal, BufPtr, toI32(Count),
                             DTypePtr, TypeID, Root, CallSiteStr});

                        // For Gather/Scatter: also type-check the recv buffer
                        // Gather(sendbuf,sendcnt,sendtype, recvbuf,recvcnt,recvtype, root,comm)
                        // Scatter(sendbuf,sendcnt,sendtype, recvbuf,recvcnt,recvtype, root,comm)
                        if (Kind == MPIKind::Gather || Kind == MPIKind::Scatter) {
                            Value *RecvBuf = getArg(3);
                            Value *RecvDType = getArg(5);
                            Value *RecvTypeID = ConstantInt::get(I32Ty, getBufferTypeID(RecvBuf));
                            Value *RecvDTypePtr = B.CreateBitOrPointerCast(RecvDType, I8PtrTy);
                            B.CreateCall(HookCheckRecvType,
                                {RecvTypeID, RecvDTypePtr, CallSiteStr});
                        }
                    }
                }
            }
        }
        return PreservedAnalyses::all();
    }
};

// ── Register the pass as a plugin ────────────────────────────────
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "MPISanitizer", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "mpi-sanitizer") {
                        MPM.addPass(MPISanPass());
                        return true;
                    }
                    return false;
                });
            // Also run automatically at -O1 and above
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel) {
                    MPM.addPass(MPISanPass());
                });
        }};
}
