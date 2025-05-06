#include "loopInvariantCodeMotion.hpp"

using namespace llvm;

/**
 * Command-line option that enables verbose output for the optimizer.
 * When enabled, the pass will print detailed information about
 * each optimization that is applied.
 *
 * Use with `-local-opts-verbose` flag when running opt.
 */
static cl::opt<bool> LICMVerbose(
    "licm-verbose",
    cl::desc(
        "Enables verbose output for the loop invariant code motion optimization"
    ),
    cl::init(false)
);

/**
    Checks if an instruction is outside the loop
*/
bool isOutsideLoop(Instruction *inst, Loop &L) {
    if (inst) return !L.contains(inst);

    return true;
}

/**
    Checks if an operand is loop invariant

    It simply checks if the operand/instruction is already contained
    in the loopInvariantInsts vector
*/
bool isOpLoopInvariant(Instruction *inst,
    std::vector<Instruction*> loopInvariantInsts
) {
    return std::find(loopInvariantInsts.begin(), loopInvariantInsts.end(), inst) !=
        loopInvariantInsts.end();
}

/**
    Checks if an instruction is loop invariant

    It uses the previously defined helper functions to simplify the logic.
    It also checks if the instruction is a branch or a return
*/
bool isLoopInvariant(Instruction &inst, Loop &L, DominatorTree &DT,
    std::vector<Instruction*> &loopInvariantInsts) {

    /*
        We don't want to hoist branch, call and return instructions,
        because this will lead to problems.

        If we hoist branch instructions, we will break the cfg.
        If we hoist call instructions, we will could have problems related to its
        side effects and return values. Moreover it would be difficult to check
        whether it is invariant.
        If we hoist return instruction we will break the cfg.
    */
    if (isa<BranchInst>(&inst)) return false;
    else if (isa<CallInst>(&inst)) return false;
    else if (isa<ReturnInst>(&inst)) return false;
    else if (isa<LoadInst>(&inst)) return false;
    else if (isa<StoreInst>(&inst)) return false;
    else if (isa<PHINode>(&inst)) return false;

    /*
        We check if the instruction can be safely executed
        by a specific LLVM function that checks whether
        an instruction has side effects or undefined behaviours,
        such as loading from invalid pointers or dividing by zero.
    */
    if (!isSafeToSpeculativelyExecute(&inst)) return false;

    /*
        For every instruction operand we check whether it is loop invariant,
        a constant or outside of the loop
    */
    for (Use &op : inst.operands()) {
        if (Instruction *opInst = dyn_cast<Instruction>(op)) {
            if (!isOpLoopInvariant(
                    dyn_cast<Instruction>(op), loopInvariantInsts
                ) &&
                !isOutsideLoop(dyn_cast<Instruction>(op), L) &&
                !isa<Constant>(op)) return false;
        }
    }


    return true;
}

bool checkDominance(Instruction &inst, DominatorTree &DT) {

    for (User *user : inst.users()){
        if (!DT.dominates(&inst, dyn_cast<Instruction>(user))) {
            return false;
        }
    }

    return true;
}

/**
    Returns a vector of loop invariant instructions in the given loop
*/
std::vector<Instruction*> getLoopInvariantInsts(Loop &L, DominatorTree &DT) {
    std::vector<Instruction*> instsToHoist;

    /*
        We check for every instruction of the loop if it is loop invariant.

        A single iteration is performed because the instruction will be already
        added to the instruction to hoist if the ir is in SSA form
        (each definition must dominates its uses).
    */
    for (BasicBlock *BB : L.getBlocks()) {
        for (Instruction &inst : *BB) {
            if (isLoopInvariant(inst, L, DT, instsToHoist) &&
                checkDominance(inst, DT)
            ) {
                instsToHoist.push_back(&inst);
            }
        }
    }

    return instsToHoist;
}

/**
    Hoists instruction from the loop's blocks up to the loop's preheader.
    It also removes repeated loads and checks for store instructions
    that use the same pointer.
*/
bool hoistInst(
    Loop &L,
    std::vector<Instruction*> &instsToHoist,
    DominatorTree &DT
) {
    bool isChanged = false;
    BasicBlock *preheader = L.getLoopPreheader();

    /*
        Performs instruction hoisting
    */
    for (Instruction *inst : instsToHoist) {
        inst->removeFromParent();
        inst->insertBefore(preheader->getTerminator());

        isChanged = true;
    }

    return isChanged;
}

/**
    Runs loop invariant code motion optimization on the given loop
*/
bool runOnLoop(Loop &L, DominatorTree &DT, ScalarEvolution &SE) {
    SCEV const *backEdgeCount = SE.getBackedgeTakenCount(&L);
    if (const SCEVConstant *tripCount = dyn_cast<SCEVConstant>(backEdgeCount)) {
        if (tripCount->getAPInt().getSExtValue() == 0) return false;
    }

    std::vector<Instruction*> loopInvariantInsts = getLoopInvariantInsts(L, DT);

    /*
        If verbose output is enabled all the invariant instructions
        are printed during the pass execution.
    */
    if (LICMVerbose) {
        outs() << "Loop Invariant instructions:\n\n";

        for (Instruction *inst : loopInvariantInsts) {
            outs() << "Instruction : ";
            inst->print(outs());
            outs() << "\n";
        }

        outs() << "\n\n";
    }

    return hoistInst(L, loopInvariantInsts, DT);
}

PreservedAnalyses LoopInvariantCodeMotion::run(
    Loop &L,
    LoopAnalysisManager &LAM,
    LoopStandardAnalysisResults &LAR,
    LPMUpdater &LU
) {
    DominatorTree &DT = LAR.DT;
    ScalarEvolution &SE = LAR.SE;

    if (runOnLoop(L, DT, SE)) return PreservedAnalyses::none();

    return PreservedAnalyses::all();
};

PassPluginLibraryInfo getLoopInvariantCodeMotionPluginInfo() {
return {LLVM_PLUGIN_API_VERSION, "LoopInvariantCodeMotion", LLVM_VERSION_STRING,
    [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, LoopPassManager &LPM,
                    ArrayRef<PassBuilder::PipelineElement>) -> bool {
                if (Name == "loop-inv-cm") {
                    LPM.addPass(LoopInvariantCodeMotion());
                    return true;
                }
                return false;
            });
    }};
}

// This is the core interface for pass plugins. It guarantees that 'opt' will
// be able to recognize TestPass when added to the pass pipeline on the
// command line, i.e. via '-passes=test-pass'
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getLoopInvariantCodeMotionPluginInfo();
}
