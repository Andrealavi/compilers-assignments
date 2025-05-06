#ifndef LLVM_TRANSFORMS_TESTPASS_H
#define LLVM_TRANSFORMS_TESTPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/ValueTracking.h"

#include <vector>
#include <algorithm>

namespace llvm {
    class LoopInvariantCodeMotion : public PassInfoMixin<LoopInvariantCodeMotion> {
        public:
            PreservedAnalyses run(
                Loop &L,
                LoopAnalysisManager &LAM,
                LoopStandardAnalysisResults &LAR,
                LPMUpdater &LU
            );
    };
} // namespace llvm

#endif // LLVM_TRANSFORMS_TESTPASS_H
