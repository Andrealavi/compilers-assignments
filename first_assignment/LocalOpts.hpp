#ifndef LLVM_TRANSFORMS_TESTPASS_H
#define LLVM_TRANSFORMS_TESTPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"

#include <cmath>
#include <map>
#include <string>
#include <algorithm>
#include <queue>

namespace llvm {
    class LocalOpts : public PassInfoMixin<LocalOpts> {
        public:
            PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
    };
    class AlgebraicIdentity : public PassInfoMixin<AlgebraicIdentity> {
            public:
                PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
    };
    class StrengthReduction : public PassInfoMixin<StrengthReduction> {
        public:
            PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
    };
    class MultiInstructionOpt : public PassInfoMixin<MultiInstructionOpt> {
        public:
            PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
    };
} // namespace llvm

#endif // LLVM_TRANSFORMS_TESTPASS _H
