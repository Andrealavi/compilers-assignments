/**
 * @author Andrea Lavino
 *
 * @file loopFusion.cpp
 * @brief Implementation of loop fusion optimization pass for LLVM
 *
 * This file contains functions to analyze and transform loops for fusion.
 * Loop fusion combines adjacent loops with equivalent iteration spaces
 * into a single loop to improve performance through better locality
 * and reduced loop overhead.
 */

#include "loopFusion.hpp"

#define CACHE_LINE_DIM 64 // CACHE LINE dimension in bytes
#define MIN_TRIP_COUNT 10 // Number of iterations needed for profitable fusion

using namespace llvm;

/* -------------------------------------------------------------------------- */
/* ----------------------------- DEBUG OPTIONS ----------------------------- */
/* -------------------------------------------------------------------------- */


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
 * Command line option to enable profitability check when applying loop fusion
 */
static cl::opt<bool> ProfitabilityCheck(
    "profitability-check",
    cl::desc(
        "Enables profitability check for loop fusion optimization"
    ),
    cl::init(false)
);

/* -------------------------------------------------------------------------- */
/* --------------------- LOOP FUSION FEASIBILITY CHECK ---------------------- */
/* -------------------------------------------------------------------------- */


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
    return L.isGuarded() ?
        L.getLoopGuardBranch()->getParent() : L.getLoopPreheader() ;
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
 * @brief Retrieves the Scalar Evolution Additive Recurrence (SCEVAddRecExpr)
 * for the memory pointer operand of a given instruction within a specific loop.
 *
 * This function first gets the SCEV expression for the pointer operand of the
 * instruction at the scope of the given loop. Then, it attempts to convert
 * this SCEV expression into an SCEVAddRecExpr.
 *
 * @param inst The instruction (typically a Load or Store)
 * whose pointer operand's SCEV is to be analyzed.
 * @param L The loop in whose context the SCEV is to be computed.
 * @param SE A reference to the ScalarEvolution analysis results.
 *
 * @return const SCEVAddRecExpr* A pointer to the SCEVAddRecExpr
 * if the pointer operand has an affine recurrence form within the loop
 * or nullptr otherwise.
 */
const SCEVAddRecExpr* getSCEVAddRec(
    Instruction &inst, Loop &L, ScalarEvolution &SE
) {
    SmallPtrSet<const SCEVPredicate *, 4> preds;

    const SCEV *inst_SCEV = SE.getSCEVAtScope(
        getLoadStorePointerOperand(&inst), &L
    );

    return SE.convertSCEVToAddRecWithPredicates(
        inst_SCEV, &L, preds
    );
}

/**
    @brief Helper function used to check if two recurrences have the same base

    Extracts each recurrence base and check their equality.
    Returns true if equal, false otherwise.
*/
bool isSameBase(
    const SCEVAddRecExpr *inst1_add_rec,
    const SCEVAddRecExpr *inst2_add_rec,
    ScalarEvolution &SE
) {
    const SCEV *inst1_base = SE.getPointerBase(inst1_add_rec);
    const SCEV *inst2_base = SE.getPointerBase(inst2_add_rec);

    if (LoopFusionVerbose) {
        outs() << "   Inst1 Base SCEV: ";
        if (inst1_base) inst1_base->print(outs());
        else outs() << "(null)";

        outs() << "\n";
        outs() << "   Inst2 Base SCEV: ";

        if (inst2_base) inst2_base->print(outs());
        else outs() << "(null)";

        outs() << "\n";
    }

    if (inst1_base != inst2_base) {
        if (LoopFusionVerbose) {
            outs() << "   Base pointers' SCEVs are different. Returning false (no provable negative distance).\n";
        }

        if (ProfitabilityCheck) {
            outs() << "The operation is not profitable\n";
            outs() << "since the base pointers are different\n";
        }

        return false;
    }

    return true;
}

/**
    @brief Computes the delta between two SCEV variables.

    @return Returns the delta between the two variables as a SCEVConstant.
*/
const SCEVConstant *getConstDelta(
    const SCEV *inst1,
    const SCEV *inst2,
    ScalarEvolution &SE
) {
    const SCEV *inst_delta = SE.getMinusSCEV(
        inst1, inst2
    );
    const SCEVConstant *const_delta = dyn_cast<SCEVConstant>(inst_delta);

    return const_delta;
}


/**
 * @brief Determines if a negative memory access distance might exist between
 * the starting memory addresses of two instructions, `inst1` and `inst2`.
 *
 * This function analyzes the memory access patterns using Scalar Evolution.
 *
 * It computes SCEVAddRecExprs for the pointer operands of both instructions.
 * - If either instruction doesn't return a SCEVAddRecExpr, or if their base
 *   pointers differ, it's assumed a negative dependence exists
 *   between these specific start pointers based on this analysis,
 *   so it returns `true`.
 * - If the base pointers are the same, it calculates the difference between
 *   their start offsets and their step offsets.
 *   - If this differences are a compile-time constant:
 *     - It returns `true` if one of the differences is >= than zero.
 *     - It returns `false` otherwise.
 *   - If one of the two differences is not a compile-time constant, this
 *     function conservatively returns `true`,
 *     indicating a potential negative distance because it cannot be disproven.
 *
 * @note SCEVAddRec is a polynomial recurrence that is written in the form:
 * `{Start, +, Step}_L`, where Start is the starting point of the recurrence,
 * Step is the step taken at each iteration and L is the Loop,
 * where this recurrence happens.
 *
 * @param L1 The loop context for `inst1`.
 * @param L2 The loop context for `inst2` (can be the same as `L1`).
 * @param SE A reference to the ScalarEvolution analysis results.
 * @param inst1 The first instruction (e.g., a store whose address is `ptr1_start + i*stride1`).
 * @param inst2 The second instruction (e.g., a load whose address is `ptr2_start + j*stride2`).
 *              This function specifically compares `ptr1_start` and `ptr2_start`.
 * @return bool `true` if a negative distance is determined
 * or conservatively assumed,
 * `false` if it's determined to be non-negative or if the preconditions
 * (same base, valid AddRecs) are not met.
 */
bool isNegativeDistance(
    Loop &L1,
    Loop &L2,
    ScalarEvolution &SE,
    Instruction &inst1,
    Instruction &inst2
) {
    if (LoopFusionVerbose) {
         outs() << "isNegativeDistance check between:\n";
         outs() << "   Inst1: " << inst1;
         if (inst1.getParent()) outs() << " (in BB: " << inst1.getParent()->getName() << ")";
         outs() << " in Loop " << (L1.getHeader() ? L1.getHeader()->getName() : "<?>") << "\n";
         outs() << "   Inst2: " << inst2;
         if (inst2.getParent()) outs() << " (in BB: " << inst2.getParent()->getName() << ")";
         outs() << " in Loop " << (L2.getHeader() ? L2.getHeader()->getName() : "<?>") << "\n";
    }

    const SCEVAddRecExpr *inst1_add_rec = getSCEVAddRec(inst1, L1, SE);
    const SCEVAddRecExpr *inst2_add_rec = getSCEVAddRec(inst2, L2, SE);

    if (LoopFusionVerbose) {
        outs() << "   Inst1 AddRec: ";

        if (inst1_add_rec) inst1_add_rec->print(outs());
        else outs() << "(null)";

        outs() << "\n";
        outs() << "   Inst2 AddRec: ";

        if (inst2_add_rec) inst2_add_rec->print(outs());
        else outs() << "(null)";

        outs() << "\n";
    }

    if (!(inst1_add_rec && inst2_add_rec)) {
        if (LoopFusionVerbose) {
            outs() << "   One or both instructions do not have a SCEVAddRecExpr. Returning true (it is not possible to assure loop fusion).\n";
        }
        return true;
    }

    if (!isSameBase(inst1_add_rec, inst2_add_rec, SE)) return false;

    const SCEV *start_inst1 = inst1_add_rec->getStart();
    const SCEV *start_inst2 = inst2_add_rec->getStart();

    const SCEVConstant *const_delta = getConstDelta(
        start_inst1, start_inst2, SE
    );

    if (LoopFusionVerbose) {
        outs() << "   Start SCEV for Inst1: ";
        if (start_inst1) start_inst1->print(outs());
        else outs() << "(null)";

        outs() << "\n";
        outs() << "   Start SCEV for Inst2: ";

        if (start_inst2) start_inst2->print(outs());
        else outs() << "(null)";

        outs() << "\n";
    }

    const SCEV *step_inst1 = inst1_add_rec->getStepRecurrence(SE);
    const SCEV *step_inst2 = inst2_add_rec->getStepRecurrence(SE);

    const SCEVConstant *const_step_delta = getConstDelta(
        step_inst1, step_inst2, SE
    );

    if (const_delta && const_step_delta) {
        if (LoopFusionVerbose) {
            outs() << "   Delta SCEV (start_inst1 - start_inst2): ";
            if (const_delta) const_delta->print(outs());
            else outs() << "(null)";
            outs() << "\n";

            outs() << "   Both deltas are constants:\n";

            outs() << "   ";
            const_delta->print(outs());
            outs() << " (Value: " <<
                const_delta->getAPInt().getSExtValue() << ")\n";

            outs() << "   ";
            const_step_delta->print(outs());
            outs() << " (Value: " <<
                const_step_delta->getAPInt().getSExtValue() << ")\n";
        }

        bool isBaseDistanceNegative = SE.isKnownPredicate(
            ICmpInst::ICMP_SLT,
            const_delta,
            SE.getZero(const_delta->getType())
        );

        bool isStepDistanceNegative = SE.isKnownPredicate(
            ICmpInst::ICMP_SLT,
            const_step_delta,
            SE.getZero(const_step_delta->getType())
        );

        if (LoopFusionVerbose) {
            outs() << "   Is base delta < 0? " <<
                (isBaseDistanceNegative ? "Yes" : "No") << "\n";

            outs() << "   Is step delta < 0? " <<
                (isStepDistanceNegative ? "Yes" : "No") << "\n";

            outs() << "   Returning " <<
                (isBaseDistanceNegative || isStepDistanceNegative ?
                    "true (negative distance detected)" :
                    "false (distance non-negative)") << ".\n";
        }


        return isBaseDistanceNegative || isStepDistanceNegative;
    }

    return true;
}

/**
 * @brief Helper function used to fill the given vectors with load and store
 * instructions
 *
 * @param L The loop where the load and store instructions are
 * @param storesVec Vector where store instructions will be placed
 * @param loadsVec Vector wheere load instructions will be placed
 */
void fillLoadVector(
    Loop &L,
    std::vector<LoadInst*> &loadsVec
) {
    for (BasicBlock *BB: L.getBlocks()) {
        for (Instruction &inst : *BB) {
            if (LoadInst *LI = dyn_cast<LoadInst>(&inst)) {
                loadsVec.push_back(LI);
            }
        }
    }
}

/**
    @brief Helper function used to fill a vector with all memory (load/store) related instructions of a Loop.
*/
void fillMemoryVector(
    Loop &L,
    std::vector<Instruction*> &memoryInsts
) {
    for (BasicBlock *BB: L.getBlocks()) {
        for (Instruction &inst : *BB) {
            if (StoreInst *SI = dyn_cast<StoreInst>(&inst)) {
                memoryInsts.push_back(SI);
            }
            if (LoadInst *LI = dyn_cast<LoadInst>(&inst)) {
                memoryInsts.push_back(LI);
            }
        }
    }
}

/**
    @brief Gets the real pointer to which a LoadInst refers.

    If working with arrays or multi-dimensional arrays, GEP instructions can hide the real memory location and, therefore, the object to which we are referring. This function is used when dealing with dependencies between loops.
*/
Value *getRealPtrValue(LoadInst *load) {
    Value *ptr = load->getPointerOperand();

    while (isa<GetElementPtrInst>(ptr)) {
        GetElementPtrInst *GEP = cast<GetElementPtrInst>(ptr);
        ptr = GEP->getPointerOperand();
    }

    return ptr;
}

/**
    @brief Gets the stores that use the given pointer.

    As for the loads, the reference of the object were we are storing the value can be hidden by GEP instructions. This function traverses the chain of GEP instructions in order to find the pointer that represent the object, which is the ptr given as an argument of the function.
*/
void getPtrStores(Value *ptr, std::vector<StoreInst*> &stores) {
    for (User *user : ptr->users()) {
        std::queue<Value*> instsToCheck;
        instsToCheck.push(user);

        while (!instsToCheck.empty()) {
            Value *inst = instsToCheck.front();
            instsToCheck.pop();

            if (StoreInst *SI = dyn_cast<StoreInst>(inst)){
                stores.push_back(SI);
            } else if (
                GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(inst)
            ) {
                for (User *gep_user : GEP->users()) {
                    instsToCheck.push(gep_user);
                }
            }
        }
    }
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
bool areDependent(
    Loop &L1, Loop &L2, ScalarEvolution &SE, DependenceInfo &DI
) {
    std::vector<LoadInst*> l2LoadInsts;

    fillLoadVector(L2, l2LoadInsts);

    if (ProfitabilityCheck && l2LoadInsts.size() == 0) {
        outs() << "The loop fusion operation is not profitable\n";
        outs() << "because there isn't any type of dependency\n";
        outs() << "between the two loops";
    }

    std::vector<StoreInst*> stores;
    for (LoadInst *load : l2LoadInsts) {
        Value *realPtrOp = getRealPtrValue(load);
        getPtrStores(realPtrOp, stores);

        for (Instruction *store : stores) {
            if (
                L1.contains(dyn_cast<Instruction>(store)) &&
                DI.depends(store, load, true) &&
                isNegativeDistance(L1, L2, SE, *store, *load)
            ) {
                return true;
            }
        }

        stores = {};
    }

    return false;
}

/**
    Function that checks whether two dependent memory instructions can exploit spatial locality. It simply checks if the sum of base and stride deltas are within the dimension of the cache line. The cache line dimension is defined arbitrarily at the beginning of this file.
*/
bool canExploitSpatialLocality(
    const SCEVConstant *base_delta,
    const SCEVConstant *stride_delta
) {
    if (!base_delta || !stride_delta) {
        outs() << "  Profitability/SpatialLocality: Base or stride delta is null, cannot determine spatial locality.\n";

        return false;
    }

    int base_delta_value = base_delta->getAPInt().getSExtValue();
    int stride_delta_value = stride_delta->getAPInt().getSExtValue();

    bool canExploit = (base_delta_value + stride_delta_value <= CACHE_LINE_DIM);

    outs() << "  Profitability/SpatialLocality: Base Delta = " << base_delta_value
            << ", Stride Delta = " << stride_delta_value
            << ", Sum = " << (base_delta_value + stride_delta_value)
            << ", Cache Line Dim = " << CACHE_LINE_DIM << "\n";

    outs() << "  Profitability/SpatialLocality: Can exploit? " << (canExploit ? "Yes" : "No") << "\n";

    return canExploit;
}

/**
    Iterates over all memory instructions within the two given loops and returns the number of times spatial locality could be used. The returned value is referenced as profitability_score.
*/
int checkSpatialLocalityUsage(
    Loop &L1,
    Loop &L2,
    std::vector<Instruction*> &l1Insts, // Changed to pass by reference
    std::vector<Instruction*> &l2Insts, // Changed to pass by reference
    ScalarEvolution &SE,
    DependenceInfo &DI
) {
    int profitability_score = 0;


    outs() << "Profitability: Checking spatial locality usage between L1 and L2 memory instructions.\n";

    outs() << "  L1 Memory Instructions (" << l1Insts.size() << "):\n";
    for (Instruction* inst : l1Insts) outs() << "    " << *inst << "\n";

    outs() << "  L2 Memory Instructions (" << l2Insts.size() << "):\n";
    for (Instruction* inst : l2Insts) outs() << "    " << *inst << "\n";

    for (Instruction *instl1 : l1Insts) {
        for (Instruction *instl2 : l2Insts) {

            outs() << "  Profitability/SpatialLocality: Checking pair: \n    L1 Inst: " << *instl1 << "\n    L2 Inst: " << *instl2 << "\n";

            if (DI.depends(instl1, instl2, true)) {
                outs() << "  Profitability/SpatialLocality: Dependence reported by DI.depends(). Analyzing access patterns.\n";

                const SCEVAddRecExpr *inst1_add_rec = getSCEVAddRec(*instl1, L1, SE);
                const SCEVAddRecExpr *inst2_add_rec = getSCEVAddRec(*instl2, L2, SE);

                outs() << "    Inst1 AddRec: ";
                if (inst1_add_rec) inst1_add_rec->print(outs()); else outs() << "(null)";
                outs() << "\n    Inst2 AddRec: ";
                if (inst2_add_rec) inst2_add_rec->print(outs()); else outs() << "(null)";
                outs() << "\n";

                if (inst1_add_rec && inst2_add_rec) {
                    if (!isSameBase(inst1_add_rec, inst2_add_rec, SE)) {
                        outs() << "    Different base pointers for SCEV. Cannot assess spatial locality for this pair based on offsets.\n";

                         continue; // Skip if bases are different for this specific profitability heuristic
                    }

                    const SCEV *start_inst1 = inst1_add_rec->getStart();
                    const SCEV *start_inst2 = inst2_add_rec->getStart();
                    const SCEVConstant *base_delta = getConstDelta(start_inst1, start_inst2, SE);

                    const SCEV *step_inst1 = inst1_add_rec->getStepRecurrence(SE);
                    const SCEV *step_inst2 = inst2_add_rec->getStepRecurrence(SE);
                    const SCEVConstant *stride_delta = getConstDelta(step_inst1, step_inst2, SE);

                    if (canExploitSpatialLocality(base_delta, stride_delta)) {
                        profitability_score++;

                        outs() << "  Profitability/SpatialLocality: Spatial locality exploitable for this pair. Score incremented to: " << profitability_score << "\n";
                    }
                } else {
                    outs() << "    Could not get SCEVAddRecExpr for one or both instructions. Cannot assess spatial locality for this pair.\n";
                }
            } else {
                outs() << "  Profitability/SpatialLocality: No dependence reported by DI.depends() for this pair. No direct spatial locality benefit counted here.\n";
            }
        }
    }

    outs() << "Profitability: Spatial locality usage score: " << profitability_score << "\n";

    return profitability_score;
}

/**
    Computes the profitability of loop fusion by considering spatial locality and number of iterations of the two given loops. If the computed score is greater than 0, the optimization is considered profitable and true is returned, otherwise it returns false.

    This way of computing profitability is extremely simple, as it does not consider other elements that could affect profitability. For example, if fusing two loops creates a unique loop that has several accesses to different objects, the operation could not be profitable, since accesses to multiple objects can saturate the cache.
*/
bool isProfitable(
    Loop &L1,
    Loop &L2,
    std::vector<Instruction*> &l1MemoryInsts,
    std::vector<Instruction*> &l2MemoryInsts,
    ScalarEvolution &SE,
    DependenceInfo &DI
) {
    outs() << "--- Starting Profitability Analysis ---\n";

    outs() << "L1 Header: "; L1.getHeader()->printAsOperand(outs(), false); outs() << "\n";
    outs() << "L2 Header: "; L2.getHeader()->printAsOperand(outs(), false); outs() << "\n";

    int profitability_score = 0;

    // 1. Check spatial locality usage
    int spatial_locality_score = checkSpatialLocalityUsage(L1, L2, l1MemoryInsts, l2MemoryInsts, SE, DI);
    profitability_score += spatial_locality_score;

    outs() << "Profitability: Score after spatial locality check: " << profitability_score << "\n";

    // 2. Check trip count
    unsigned tripCountL1 = SE.getSmallConstantTripCount(&L1);
    if (tripCountL1 > MIN_TRIP_COUNT) {
        profitability_score++;

        outs() << "Profitability: L1 trip count (" << tripCountL1 << ") > MIN_TRIP_COUNT (" << MIN_TRIP_COUNT << "). Score incremented.\n";
    } else {
        outs() << "Profitability: L1 trip count (" << tripCountL1 << ") <= MIN_TRIP_COUNT (" << MIN_TRIP_COUNT << "). No score for trip count.\n";
    }

    outs() << "Profitability: Score after trip count check: " << profitability_score << "\n";

    outs() << "Profitability: Final Profitability Score: " << profitability_score << "\n";

    outs() << "--- End of Profitability Analysis (" << (profitability_score > 0 ? "PROFITABLE" : "NOT PROFITABLE") << ") ---\n";

    return profitability_score > 0;
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

        bool dependent = areDependent(L1, L2, SE, DI);
        if (dependent) {
            if (LoopFusionVerbose) errs() << "Loops have dependencies - fusion not possible\n";
            return false;
        }

        if (LoopFusionVerbose) {
            errs() << "All checks passed - loop fusion is applicable!\n";
        }
        return true;
}

/* -------------------------------------------------------------------------- */
/* ------------------------- LOOP FUSION APPLICATION ------------------------ */
/* -------------------------------------------------------------------------- */

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


// It can be useful to check that the induction variables are relatable (i.e. they have the same start)
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
 * @brief Fuse two loop headers into a single header
 *
 * Moves instructions from the second loop header to the first loop header
 * and updates branch instructions and successors.
 *
 * @param l1Header Header of the first loop
 * @param l2Header Header of the second loop
 * @param l1First First body block of the first loop
 */
// Serve per gestire le phi functions
void movePN(BasicBlock *l1Header, BasicBlock *l2Header, BasicBlock *l2Exit) {
    if (LoopFusionVerbose) {
        errs() << "Fusing headers:\n";
        errs() << "  L1 header: ";
        l1Header->printAsOperand(errs(), false);
        errs() << "\n  L2 header: ";
        l2Header->printAsOperand(errs(), false);
        errs() << "\n  L1 first body block: ";
        l2Exit->printAsOperand(errs(), false);
        errs() << "\n";
    }

    Instruction *l1termInst = l1Header->getTerminator();
    Instruction *l2termInst = l2Header->getTerminator();

    std::vector<PHINode*> instsToMove;

    for (Instruction &inst : *l2Header) {
        if (PHINode *PN = dyn_cast<PHINode>(&inst)) {
            instsToMove.push_back(PN);
        }
    }

    if (LoopFusionVerbose) {
        errs() << "Moving " << instsToMove.size() << " instructions from L2 header to L1 header\n";
    }

    for (PHINode *PN : instsToMove) {
        if (!PN->hasNUses(0)) {
            if (LoopFusionVerbose) {
                errs() << "  Moving PHI node: ";
                PN->print(errs());
                errs() << "\n";
            }
            PN->moveBefore(l1Header->getFirstNonPHI());


            // Predecessors are processed in inverse order
            // This could be done without a for loop by just considering
            // the loop preheader and the loop latch
            // however in this function I do not have that information
            // probably I could change this
            int pred_idx = 1;
            for (BasicBlock *BB : predecessors(l1Header)) {
                PN->setIncomingBlock(pred_idx--, BB);
            }
        }
    }

    l1termInst->setSuccessor(1, l2Exit);

    if (LoopFusionVerbose) {
        errs() << "Erasing L2 header\n";
    }
}

/**
 * @brief Helper function for fusing loops bodies
 *
 * It fuse the loop bodies by moving all the instructions
 * from the first block of the second loop to the last
 * block of the first loop
 */
void fuseBodies(BasicBlock *l1Last, BasicBlock *l2First) {
    Instruction *l1LastTerm = l1Last->getTerminator();
    std::vector<Instruction*> instsToMove;

    for (Instruction &inst : *l2First) {
        instsToMove.push_back(&inst);
    }

    for (Instruction *inst : instsToMove) {
        inst->moveBefore(l1LastTerm);
    }

    l2First->replaceAllUsesWith(l1Last);

    l2First->eraseFromParent();
    l1LastTerm->eraseFromParent();
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
            inductionVariable->eraseFromParent();
        }
    }

    if (LoopFusionVerbose) {
        errs() << "Replacing L2 preheader with L1 preheader\n";
        errs() << "Replacing L2 latch with L1 latch\n";
    }

    L2.getLoopLatch()->replaceAllUsesWith(L1.getLoopLatch());
    L2.getLoopLatch()->eraseFromParent();

    movePN(l1Header, l2Header, l2Exit);

    if (LoopFusionVerbose) {
        errs() << "Connecting L1 last block to L2 first block\n";
    }

    if (l2First == l2Last)  l2Last = l1Last;

    fuseBodies(l1Last, l2First);

    if (LoopFusionVerbose) {
        errs() << "Connecting L2 last block to L1 latch\n";
    }

    inst = BranchInst::Create(L1.getLoopLatch(), l2Last->getTerminator());
    l2Last->getTerminator()->eraseFromParent();

    if (LoopFusionVerbose) {
        errs() << "Loop fusion completed successfully\n";
    }

    l2Preheader->eraseFromParent();
    l2Header->eraseFromParent();
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
void updateLoopInfo(
    Function &F,
    FunctionAnalysisManager &AM,
    DominatorTree *&DT,
    PostDominatorTree *&PDT,
    ScalarEvolution *&SE,
    DependenceInfo *&DI,
    LoopInfo *&LI
) {
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
        errs() << "Running LoopFusion pass on function: "
            << F.getName() << "\n";
    }

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

        for (
            auto first_it = loops.begin(); first_it != loops.end() && !isLoopFusionApplied; ++first_it
        ) {
            Loop *L1 = *first_it;

            for (
                auto second_it = std::next(first_it);
                second_it != loops.end() && !isLoopFusionApplied;
                ++second_it
            ) {
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
                    if (ProfitabilityCheck) {
                        outs() << "\n===== Profitability Check for Loop Fusion =====\n";
                        outs() << "L1 Header: "; L1->getHeader()->printAsOperand(outs(), false); outs() << "\n";
                        outs() << "L2 Header: "; L2->getHeader()->printAsOperand(outs(), false); outs() << "\n";

                        std::vector<Instruction*> l1MemoryInsts;
                        std::vector<Instruction*> l2MemoryInsts;

                        fillMemoryVector(*L1, l1MemoryInsts);
                        fillMemoryVector(*L2, l2MemoryInsts);

                        if (!isProfitable(*L1, *L2, l1MemoryInsts, l2MemoryInsts,  *SE, *DI)) {
                            outs() << "Profitability Check: Loop fusion deemed NOT PROFITABLE.\n";
                        } else {
                            outs() << "Profitability Check: Loop fusion deemed PROFITABLE.\n";
                        }
                        outs() << "===== End of Profitability Check =====\n\n";
                    }

                    applyLoopFusion(*L1, *L2);
                    fusionCount++;

                    if (LoopFusionVerbose) {
                        errs() << "Successfully applied fusion #" << fusionCount << "\n";
                    }

                    isLoopFusionApplied = true;
                }
            }
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

/* -------------------------------------------------------------------------- */
/* -------------------------- PLUGIN REGISTRATION --------------------------- */
/* -------------------------------------------------------------------------- */

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
