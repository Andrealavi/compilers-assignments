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
    Checks if a load instruction is loop invariant

    A load instruction is loop invariant if it is not followed by a store or if
    it dominates it and the stored value is equal
    to the register where the value was loaded
*/
bool isLoadLoopInvariant(LoadInst &inst, Loop &L, DominatorTree &DT,
    std::vector<Instruction*> &loopInvariantInsts) {
    Value *ptr = inst.getPointerOperand();

    for (User *user: ptr->users()) {
        if(StoreInst *SI = dyn_cast<StoreInst>(user)) {
            Instruction *storeInstOperand =
                dyn_cast<Instruction>(SI->getValueOperand());
            if (DT.dominates(&inst, SI) && storeInstOperand != &inst)
                return false;
            else if (
                !isOutsideLoop(SI, L) &&
                std::find(
                    loopInvariantInsts.begin(),
                    loopInvariantInsts.end(),
                    SI
                ) == loopInvariantInsts.end()
            ) return false;
        }
    }

    return true;
}

/**
    Checks wheter a store is loop invariant

    A store instruction is loop invariant if it dominates all the loads that use
    the same pointer and the value operand is loop invariant as well
*/
bool isStoreLoopInvariant(
    StoreInst &inst, Loop &L,
    DominatorTree &DT, std::vector<Instruction*> &loopInvariantInsts) {

    if (
        std::find(
            loopInvariantInsts.begin(),
            loopInvariantInsts.end(),
            inst.getValueOperand()
        ) == loopInvariantInsts.end()
    ) return false;

    for (User *user : inst.getPointerOperand()->users()) {
        if (LoadInst *LI = dyn_cast<LoadInst>(user)) {
            if (!DT.dominates(&inst, LI) && !isOutsideLoop(LI, L)) return false;
        }
    }

    return true; }

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
    if (BranchInst *BI = dyn_cast<BranchInst>(&inst)) return false;
    else if (CallInst *CI = dyn_cast<CallInst>(&inst)) return false;
    else if (ReturnInst *RI = dyn_cast<ReturnInst>(&inst)) return false;

    /*
        If we have a load or a store we have to check if they are loop invariant
        or not. If we use mem2reg pass these checks will be avoided, because
        there won't be any load/store.
    */
    if (LoadInst *LI = dyn_cast<LoadInst>(&inst))
        return isLoadLoopInvariant(*LI, L, DT, loopInvariantInsts);
    else if (StoreInst *SI = dyn_cast<StoreInst>(&inst))
        return isStoreLoopInvariant(*SI, L, DT, loopInvariantInsts);

    /*
        For every instruction operand we check
    */
    for (Use &op : inst.operands()) {
        if (Instruction *opInst = dyn_cast<Instruction>(op)) {
            if (!isOpLoopInvariant(
                    dyn_cast<Instruction>(op), loopInvariantInsts
                ) &&
                !isOutsideLoop(dyn_cast<Instruction>(op), L) &&
                !isa<ConstantInt>(op)) return false;
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
            if (isLoopInvariant(inst, L, DT, instsToHoist)) {
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
bool hoistInst(Loop &L, std::vector<Instruction*> &loopInvariantInsts) {
    bool isChanged = false;
    BasicBlock *preheader = L.getLoopPreheader();

    std::vector<Instruction*> instsToHoist;

    for (Instruction *inst : loopInvariantInsts) {
        bool canBeHoisted = true;

        /*
            If the pointer operand is used in another store instruction within the loop, the instruction should not be hoisted. This means that
            there are multiple definitions of the same variable.
        */
        if (StoreInst *SI = dyn_cast<StoreInst>(inst)) {
            Value *ptr = SI->getPointerOperand();

            for (User *user : ptr->users()) {
                if (StoreInst *userSI = dyn_cast<StoreInst>(user)) {
                    if (user != inst && !isOutsideLoop(userSI, L))
                        canBeHoisted = false;
                }
            }
        } else if (LoadInst *LI = dyn_cast<LoadInst>(inst)) {
            /*
                If the pointer operand is used in another instruction
                that is invariant as well, the current instruction
                is removed and not hoisted since it would be a redundant
                operation.
            */
            Value *ptr = LI->getPointerOperand();

            for (User *user : ptr->users()) {
                if (LoadInst *userLI = dyn_cast<LoadInst>(user)) {
                    if (user != inst &&
                        std::find(
                            instsToHoist.begin(), instsToHoist.end(), user
                        ) != instsToHoist.end()) {
                        canBeHoisted = false;
                        inst->replaceAllUsesWith(user);
                        inst->eraseFromParent();
                    }
                }
            }

        }

        if (canBeHoisted) instsToHoist.push_back(inst);
    }

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
bool runOnLoop(Loop &L, DominatorTree &DT) {
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

    return hoistInst(L, loopInvariantInsts);
}

PreservedAnalyses LoopInvariantCodeMotion::run(
    Loop &L,
    LoopAnalysisManager &LAM,
    LoopStandardAnalysisResults &LAR,
    LPMUpdater &LU
) {
    DominatorTree &DT = LAR.DT;

    if (runOnLoop(L, DT)) return PreservedAnalyses::none();

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
