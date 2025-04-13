/**
 * @file loopFusion.hpp
 * @brief Header for the loop fusion optimization pass
 *
 * Defines the LoopFusion class, a pass that analyzes and combines adjacent loops
 * with the same iteration space to improve performance. Loop fusion typically
 * improves cache locality and reduces loop overhead.
 */

#ifndef LLVM_TRANSFORMS_TESTPASS_H
#define LLVM_TRANSFORMS_TESTPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/Scalar/LoopSimplifyCFG.h"

#include <map>

namespace llvm {
    /**
     * @class LoopFusion
     * @brief LLVM optimization pass that fuses adjacent compatible loops
     *
     * This optimization pass identifies and combines loops that:
     * - Are adjacent in the control flow graph
     * - Have the same trip count (number of iterations)
     * - Are control flow equivalent
     * - Don't have loop-carried dependencies between them
     */
    class LoopFusion : public PassInfoMixin<LoopFusion> {
        public:
            /**
             * @brief Run loop fusion optimization on a function
             *
             * Analyzes the function for fusion opportunities and applies
             * transformations where possible. Loops are fused iteratively
             * until no more opportunities are found.
             *
             * @param F Function to optimize
             * @param AM Function analysis manager
             * @return PreservedAnalyses Analysis preservation info
             */
            PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
    };
} // namespace llvm

#endif // LLVM_TRANSFORMS_TESTPASS_H
