/**
 * @file loopFusion.cpp
 * @brief Implementation of loop fusion optimization pass for LLVM
 *
 * This file contains functions to analyze and transform loops for fusion.
 * Loop fusion combines adjacent loops with equivalent iteration spaces
 * into a single loop to improve performance through better locality
 * and reduced loop overhead.
 */

#include "loopFusion.hpp"

using namespace llvm;

/**
 * Command line option to enable verbose debug output for the loop fusion pass
 */
static cl::opt<bool> LoopFusionVerbose(
    "lf-verbose",
    cl::desc(
        "Enables verbose output for loop fusion optimization"
    ),
    cl::init(false)
);

/**
 * @brief Get the block to check for adjacency (guard block or preheader)
 *
 * For guarded loops, returns the guard branch block, otherwise returns
 * the loop preheader block.
 *
 * @param L The loop to analyze
 * @return BasicBlock* The block to check for adjacency
 */
BasicBlock* getBlockToCheck(Loop &L) {
    return L.isGuarded() ? L.getLoopGuardBranch()->getParent() : L.getLoopPreheader() ;
}

/**
 * @brief Determine if two loops are adjacent in the control flow graph
 *
 * Checks if the exit block of the first loop is the entry block of the second loop.
 * This is a necessary condition for loop fusion.
 *
 * @param L1 First loop
 * @param L2 Second loop
 * @return true If the loops are adjacent
 * @return false Otherwise
 */
bool areAdjacents(Loop &L1, Loop &L2) {
    BasicBlock *L2Entry = getBlockToCheck(L2);
    BasicBlock *L1Exit = nullptr;

    if (L1.isGuarded()) {
        L1Exit = dyn_cast<BasicBlock>(L1.getLoopGuardBranch()->getOperand(1));
    } else {
        L1Exit = L1.getExitBlock();
    }

    if (LoopFusionVerbose) {
        errs() << "Checking if loops are adjacent:\n";
        errs() << "  L1 exit block: ";
        if (L1Exit)
            L1Exit->printAsOperand(errs(), false);
        else
            errs() << "nullptr";
        errs() << "\n  L2 entry block: ";
        if (L2Entry)
            L2Entry->printAsOperand(errs(), false);
        else
            errs() << "nullptr";
        errs() << "\n  Adjacent: " << (L2Entry == L1Exit ? "Yes" : "No") << "\n";
    }

    return L2Entry == L1Exit;
}

/**
 * @brief Check for control flow equivalence between loops
 *
 * Two loops are control flow equivalent if the first loop's header dominates
 * the second loop's header, and the second loop's header post-dominates the
 * first loop's header. This ensures proper nesting relationship.
 *
 * @param L1 First loop
 * @param L2 Second loop
 * @param DT Dominator tree analysis
 * @param PDT Post-dominator tree analysis
 * @return true If the loops are control flow equivalent
 * @return false Otherwise
 */
bool areCFE(Loop &L1, Loop &L2, DominatorTree &DT, PostDominatorTree &PDT) {
    bool result = DT.dominates(L1.getHeader(), L2.getHeader()) &&
        PDT.dominates(L2.getHeader(), L1.getHeader());

    if (LoopFusionVerbose) {
        errs() << "Checking control flow equivalence:\n";
        errs() << "  L1 header dominates L2 header: "
               << (DT.dominates(L1.getHeader(), L2.getHeader()) ? "Yes" : "No") << "\n";
        errs() << "  L2 header post-dominates L1 header: "
               << (PDT.dominates(L2.getHeader(), L1.getHeader()) ? "Yes" : "No") << "\n";
        errs() << "  CFE result: " << (result ? "Yes" : "No") << "\n";
    }

    return result;
}

/**
 * @brief Check if two loops have the same iteration count
 *
 * Uses ScalarEvolution to determine if the loops execute the same number of iterations.
 * This is a requirement for fusion, as loops with different iteration counts
 * cannot be directly combined.
 *
 * @note The trip count could be reported as 0 if the ScalarEvolution analysis
 * cannot recognize the start and end values of the loop. This commonly happens
 * when the loop doesn't use traditional PHI nodes for induction variables but
 * instead uses loads and stores. In such cases, the function will return false
 * even though the loops might actually have the same iteration count.
 *
 * @param L1 First loop
 * @param L2 Second loop
 * @param SE Scalar evolution analysis
 * @return true If both loops have matching non-zero iteration counts
 * @return false Otherwise
 */
bool haveSameItNum(Loop &L1, Loop &L2, ScalarEvolution &SE) {
    unsigned L1TripCount = SE.getSmallConstantTripCount(&L1);
    unsigned L2TripCount = SE.getSmallConstantTripCount(&L2);
    bool result = (L1TripCount == L2TripCount) && (L1TripCount != 0);

    if (LoopFusionVerbose) {
        errs() << "Checking trip counts:\n";
        errs() << "  L1 trip count: " << L1TripCount << "\n";
        errs() << "  L2 trip count: " << L2TripCount << "\n";
        errs() << "  Same trip count: " << (result ? "Yes" : "No") << "\n";
    }

    return result;
}

/**
 * @brief Check if a load instruction depends on any of the store instructions
 *
 * Uses DependenceInfo to determine if there's a dependence between the load
 * and any of the stores, which would prevent fusion.
 *
 * @param inst Load instruction to check
 * @param storeInsts Vector of store instructions to check against
 * @param DI Dependence information analysis
 * @return true If a dependence is found
 * @return false Otherwise
 */
bool isDependent(LoadInst &inst, std::vector<StoreInst*> storeInsts, DependenceInfo &DI) {
    for (StoreInst *storeInst : storeInsts) {
        if (DI.depends(&inst, storeInst, true)) {
            if (LoopFusionVerbose) {
                errs() << "  Found dependency between:\n";
                errs() << "    Load: ";
                inst.print(errs());
                errs() << "\n    Store: ";
                storeInst->print(errs());
                errs() << "\n";
            }
            return true;
        }
    }

    return false;
}

/**
 * @brief Collect all store instructions from a loop
 *
 * @param L Loop to analyze
 * @return std::vector<StoreInst*> Vector of store instructions in the loop
 */
std::vector<StoreInst*> getLoopStores(Loop &L) {
    std::vector<StoreInst*> loopStores;

    for (Instruction &inst : *L.getHeader()) {
        if (StoreInst *SI = dyn_cast<StoreInst>(&inst)) loopStores.push_back(SI);
    }

    for (BasicBlock *BB : L.getBlocks()) {
        for (Instruction &inst : *BB) {
            if (StoreInst *SI = dyn_cast<StoreInst>(&inst)) loopStores.push_back(SI);
        }
    }

    if (LoopFusionVerbose) {
        errs() << "Collected " << loopStores.size() << " store instructions from loop\n";
    }

   return loopStores;
}

/**
 * @brief Check for loop-carried dependencies between two loops
 *
 * Determines if there are any data dependencies from the first loop to the second
 * that would prevent fusion.
 *
 * @param L1 First loop
 * @param L2 Second loop
 * @param DI Dependence information analysis
 * @return true If there are dependencies preventing fusion
 * @return false Otherwise
 */
bool areDependent(Loop &L1, Loop &L2, DependenceInfo &DI) {
    if (LoopFusionVerbose) {
        errs() << "Checking for dependencies between loops...\n";
    }

    std::vector<StoreInst*> l1Stores = getLoopStores(L1);

    for (BasicBlock *BB : L2.getBlocks()) {
        for (Instruction &inst : *BB) {
            if (LoadInst *LI = dyn_cast<LoadInst>(&inst)) {
                if (isDependent(*LI, l1Stores, DI)) {
                    if (LoopFusionVerbose) {
                        errs() << "Loop dependency found - fusion not possible\n";
                    }
                    return true;
                }
            }
        }
    }

    if (LoopFusionVerbose) {
        errs() << "No loop dependencies found\n";
    }

    return false;
}

/**
 * @brief Check if loop fusion is applicable between two loops
 *
 * Performs all the necessary checks to determine if two loops can be fused:
 * - Loops must be adjacent
 * - Loops must be control flow equivalent
 * - Loops must have the same trip count
 * - No loop-carried dependencies between loops
 *
 * @param L1 First loop
 * @param L2 Second loop
 * @param SE Scalar evolution analysis
 * @param DT Dominator tree analysis
 * @param PDT Post-dominator tree analysis
 * @param DI Dependence information analysis
 * @return true If fusion can be applied
 * @return false Otherwise
 */
bool isLoopFusionApplicable(Loop &L1, Loop &L2, ScalarEvolution &SE,
    DominatorTree &DT, PostDominatorTree &PDT, DependenceInfo &DI) {
        if (LoopFusionVerbose) {
            errs() << "\n===== Checking if loop fusion is applicable =====\n";
            errs() << "Loop 1 header: ";
            L1.getHeader()->printAsOperand(errs(), false);
            errs() << "\nLoop 2 header: ";
            L2.getHeader()->printAsOperand(errs(), false);
            errs() << "\n";
        }

        bool adjacent = areAdjacents(L1, L2);
        if (!adjacent) {
            if (LoopFusionVerbose) errs() << "Loops are not adjacent - fusion not possible\n";
            return false;
        }

        bool cfe = areCFE(L1, L2, DT, PDT);
        if (!cfe) {
            if (LoopFusionVerbose) errs() << "Loops are not control flow equivalent - fusion not possible\n";
            return false;
        }

        bool sameIter = haveSameItNum(L1, L2, SE);
        if (!sameIter) {
            if (LoopFusionVerbose) errs() << "Loops don't have same iteration count - fusion not possible\n";
            return false;
        }

        bool dependent = areDependent(L1, L2, DI);
        if (dependent) {
            if (LoopFusionVerbose) errs() << "Loops have dependencies - fusion not possible\n";
            return false;
        }

        if (LoopFusionVerbose) {
            errs() << "All checks passed - loop fusion is applicable!\n";
        }
        return true;
}

/**
 * @brief Get the first body block of a loop after the header
 *
 * @param L Loop to analyze
 * @return BasicBlock* First body block or nullptr if not found
 */
BasicBlock *getFirstBodyBlock(Loop &L) {
    BasicBlock *BB = L.getHeader();

    for (BasicBlock *succ : successors(BB)) {
        if (L.contains(succ) && succ != BB) return succ;
    }

    return nullptr;
}

/**
 * @brief Get the last body block of a loop before the latch
 *
 * @param L Loop to analyze
 * @return BasicBlock* Last body block or nullptr if not found
 */
BasicBlock *getLastBodyBlock(Loop &L) {
    BasicBlock *BB = L.getLoopLatch();

    for (BasicBlock *pred : predecessors(BB)) {
        if (L.contains(pred) && pred != BB) return pred;
    }

    return nullptr;
}

/**
 * @brief Find the induction variable PHI node in a loop header
 *
 * Searches for a PHI node that is used in a comparison that controls the loop's
 * branch instruction.
 *
 * @param L Loop to analyze
 * @return PHINode* Induction variable or nullptr if not found
 */
PHINode* getInductionVariable(Loop &L) {
    BasicBlock *header = L.getHeader();

    for (Instruction &inst : *header) {
        if (PHINode *PN = dyn_cast<PHINode>(&inst)) {
            for (User *user : PN->users()) {
                if (CmpInst *CI = dyn_cast<CmpInst>(user)) {
                    for (User *cmpUser : CI->users()) {
                        if (BranchInst *BI = dyn_cast<BranchInst>(cmpUser)) {
                            if (BI->isConditional()) return PN;
                        }
                    }
                }
            }
        }
    }

    return nullptr;
}

/**
 * @brief Check if a loop has a preheader block
 *
 * @param L Loop to check
 * @return true If the loop has a preheader
 * @return false Otherwise
 */
bool hasPreheader(Loop &L) {
    return L.getLoopPreheader() != nullptr;
}

/**
 * @brief Remove an unused compare instruction and its associated load instruction
 *
 * Called during loop fusion cleanup to remove redundant instructions.
 *
 * @param inst Compare instruction to remove
 */
void removeCompareAndLoad(CmpInst *inst) {
    if (inst->hasNUses(0)) {
        if (LoopFusionVerbose) {
            errs() << "Removing unused compare instruction: ";
            inst->print(errs());
            errs() << "\n";
        }

        if (LoadInst *LI = dyn_cast<LoadInst>(inst->getOperand(0))) {
            if (LoopFusionVerbose) {
                errs() << "Removing associated load instruction: ";
                LI->print(errs());
                errs() << "\n";
            }
            LI->eraseFromParent();
        }

        inst->eraseFromParent();
    }
}

/**
 * @brief Fuse two loop headers into a single header
 *
 * Moves instructions from the second loop header to the first loop header
 * and updates branch instructions and successors.
 *
 * @param l1Header Header of the first loop
 * @param l2Header Header of the second loop
 * @param l1First First body block of the first loop
 */
void fuseHeader(BasicBlock *l1Header, BasicBlock *l2Header, BasicBlock *l1First) {
    if (LoopFusionVerbose) {
        errs() << "Fusing headers:\n";
        errs() << "  L1 header: ";
        l1Header->printAsOperand(errs(), false);
        errs() << "\n  L2 header: ";
        l2Header->printAsOperand(errs(), false);
        errs() << "\n  L1 first body block: ";
        l1First->printAsOperand(errs(), false);
        errs() << "\n";
    }

    Instruction *l1termInst = l1Header->getTerminator();
    Instruction *l2termInst = l2Header->getTerminator();

    std::vector<Instruction*> instsToMove;

    for (Instruction &inst : *l2Header) {
        if (&inst != l2termInst) instsToMove.push_back(&inst);
    }

    if (LoopFusionVerbose) {
        errs() << "Moving " << instsToMove.size() << " instructions from L2 header to L1 header\n";
    }

    for (Instruction *inst : instsToMove) {
        if (PHINode *PN = dyn_cast<PHINode>(inst)) {
            if (!PN->hasNUses(0)) {
                if (LoopFusionVerbose) {
                    errs() << "  Moving PHI node: ";
                    PN->print(errs());
                    errs() << "\n";
                }
                PN->moveBefore(l1Header->getFirstNonPHI());
            }
        } else if (inst != l2termInst) {
            if (LoopFusionVerbose) {
                errs() << "  Moving instruction: ";
                inst->print(errs());
                errs() << "\n";
            }
            inst->moveBefore(l1termInst);
        }
    }

    l2termInst->moveAfter(l1termInst);
    l1termInst->eraseFromParent();
    l2termInst->setSuccessor(0, l1First);

    std::vector<BasicBlock*> bbToErase;

    for (BasicBlock *BB : predecessors(l2Header)) {
        if (BB->hasNPredecessors(0)) bbToErase.push_back(BB);
    }

    if (LoopFusionVerbose && !bbToErase.empty()) {
        errs() << "Removing " << bbToErase.size() << " unreachable predecessor blocks\n";
    }

    for (BasicBlock *BB : bbToErase) BB->eraseFromParent();

    if (LoopFusionVerbose) {
        errs() << "Erasing L2 header\n";
    }
    l2Header->eraseFromParent();
}

/**
 * @brief Apply loop fusion transformation to merge two loops
 *
 * Performs the actual transformation to fuse two loops, including:
 * - Replacing induction variables
 * - Redirecting control flow
 * - Merging headers
 * - Connecting loop bodies
 *
 * @param L1 First loop
 * @param L2 Second loop
 */
void applyLoopFusion(Loop &L1, Loop &L2) {
    if (LoopFusionVerbose) {
        errs() << "\n===== Applying loop fusion =====\n";
        errs() << "L1 header: ";
        L1.getHeader()->printAsOperand(errs(), false);
        errs() << "\nL2 header: ";
        L2.getHeader()->printAsOperand(errs(), false);
        errs() << "\n";
    }

    BasicBlock *l1First = getFirstBodyBlock(L1);
    BasicBlock *l1Last = getLastBodyBlock(L1);

    BasicBlock *l2First = getFirstBodyBlock(L2);
    BasicBlock *l2Last = getLastBodyBlock(L2);

    BasicBlock *l1Header = L1.getHeader();

    BasicBlock *l2Preheader = L2.getLoopPreheader();
    BasicBlock *l2Header = L2.getHeader();
    BasicBlock *l2Exit = L2.getExitBlock();

    BranchInst *inst = nullptr;

    PHINode *inductionVariable = getInductionVariable(L2);
    if (inductionVariable) {
        PHINode *l1InductionVar = getInductionVariable(L1);

        if (l1InductionVar) {
            if (LoopFusionVerbose) {
                errs() << "Replacing L2 induction variable with L1 induction variable\n";
                errs() << "  L1 IV: ";
                l1InductionVar->print(errs());
                errs() << "\n  L2 IV: ";
                inductionVariable->print(errs());
                errs() << "\n";
            }
            inductionVariable->replaceAllUsesWith(l1InductionVar);
        }
    }

    if (LoopFusionVerbose) {
        errs() << "Replacing L2 preheader with L1 preheader\n";
        errs() << "Replacing L2 latch with L1 latch\n";
    }

    L2.getLoopPreheader()->replaceAllUsesWith(L1.getLoopPreheader());
    L2.getLoopLatch()->replaceAllUsesWith(L1.getLoopLatch());

    fuseHeader(l1Header, l2Header, l1First);

    if (LoopFusionVerbose) {
        errs() << "Connecting L1 last block to L2 first block\n";
    }
    inst = BranchInst::Create(l2First, l1Last->getTerminator());
    l1Last->getTerminator()->eraseFromParent();

    if (LoopFusionVerbose) {
        errs() << "Connecting L2 last block to L1 latch\n";
    }
    inst = BranchInst::Create(L1.getLoopLatch(), l2Last->getTerminator());
    l2Last->getTerminator()->eraseFromParent();

    if (LoopFusionVerbose) {
        errs() << "Loop fusion completed successfully\n";
    }
}

/**
 * @brief Update loop analysis information after fusion
 *
 * Refreshes all the analysis results for a function after loop transformations.
 *
 * @param F Function to update
 * @param AM Analysis manager
 * @param DT Dominator tree analysis (output)
 * @param PDT Post-dominator tree analysis (output)
 * @param SE Scalar evolution analysis (output)
 * @param DI Dependence information analysis (output)
 * @param LI Loop information analysis (output)
 */
void updateLoopInfo(Function &F, FunctionAnalysisManager &AM, DominatorTree *&DT,
    PostDominatorTree *&PDT, ScalarEvolution *&SE, DependenceInfo *&DI, LoopInfo *&LI) {
        if (LoopFusionVerbose) {
            errs() << "Updating loop analysis information\n";
        }
        DT = &AM.getResult<DominatorTreeAnalysis>(F);
        PDT = &AM.getResult<PostDominatorTreeAnalysis>(F);
        SE = &AM.getResult<ScalarEvolutionAnalysis>(F);
        DI = &AM.getResult<DependenceAnalysis>(F);
        LI = &AM.getResult<LoopAnalysis>(F);
}

/**
 * @brief Main loop fusion pass implementation
 *
 * Iteratively applies loop fusion to eligible loop pairs in a function.
 * After each fusion, analysis information is updated and another fusion
 * attempt is made until no more fusions are possible.
 *
 * @param F Function to optimize
 * @param AM Function analysis manager
 * @return PreservedAnalyses Analysis preservation info (none preserved)
 */
PreservedAnalyses LoopFusion::run(Function &F, FunctionAnalysisManager &AM) {
    if (LoopFusionVerbose) {
        errs() << "Running LoopFusion pass on function: " << F.getName() << "\n";
    }

    bool transformed = false;

    DominatorTree *DT = nullptr;
    PostDominatorTree *PDT = nullptr;
    ScalarEvolution *SE = nullptr;
    DependenceInfo *DI = nullptr;
    LoopInfo *LI = nullptr;

    bool isLoopFusionApplied;
    int fusionCount = 0;

    do {
        isLoopFusionApplied = false;

        updateLoopInfo(F, AM, DT, PDT, SE, DI, LI);

        SmallVector<Loop*, 4> loops = LI->getLoopsInPreorder();

        if (LoopFusionVerbose) {
            errs() << "Found " << loops.size() << " loops in function\n";
        }

        for (auto first_it = loops.begin(); first_it != loops.end(); ++first_it) {
            Loop *L1 = *first_it;

            for (auto second_it = std::next(first_it); second_it != loops.end(); ++second_it) {
                Loop *L2 = *second_it;

                if (LoopFusionVerbose) {
                    errs() << "\nAttempting to fuse loops:\n";
                    errs() << "  Loop 1 header: ";
                    L1->getHeader()->printAsOperand(errs(), false);
                    errs() << "\n  Loop 2 header: ";
                    L2->getHeader()->printAsOperand(errs(), false);
                    errs() << "\n";
                }

                if (isLoopFusionApplicable(*L1, *L2, *SE, *DT, *PDT, *DI)) {
                    applyLoopFusion(*L1, *L2);
                    fusionCount++;

                    if (LoopFusionVerbose) {
                        errs() << "Successfully applied fusion #" << fusionCount << "\n";
                    }

                    isLoopFusionApplied = true;
                    transformed = true;
                    break;
                }
            }

            if (isLoopFusionApplied) break;
        }

        if (isLoopFusionApplied) {
            if (LoopFusionVerbose) {
                errs() << "Invalidating analysis after fusion\n";
            }
            AM.invalidate(F, PreservedAnalyses::none());
        }
    } while(isLoopFusionApplied);

    if (LoopFusionVerbose) {
        errs() << "\nLoop Fusion pass complete - applied " << fusionCount
               << " fusion" << (fusionCount != 1 ? "s" : "") << "\n";
    }

    return PreservedAnalyses::none();
}

/**
 * @brief Get plugin registration information for the loop fusion pass
 *
 * Creates registration info that allows the pass to be loaded by LLVM
 * when specified through command line options.
 *
 * @return PassPluginLibraryInfo Registration information
 */
PassPluginLibraryInfo getLoopFusionPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LoopFusion", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // Register the pass with the pass builder
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) -> bool {
                    // Allow the pass to be invoked via -passes=lf
                    if (Name == "lf") {
                        FPM.addPass(LoopSimplifyPass());
                        FPM.addPass(LoopFusion());
                        return true;
                    }
                    return false;
                });
        }};
}

/**
 * Plugin API entry point - allows opt to recognize the pass
 * when used with -passes=lf command line option.
 *
 * This is called by LLVM when loading the pass to get information
 * about it and register it in the pass pipeline.
 *
 * @return PassPluginLibraryInfo for this pass
 */
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getLoopFusionPluginInfo();
}
