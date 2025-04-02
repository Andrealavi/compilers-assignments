#include "dominatorAnalysis.hpp"
#include <llvm-19/llvm/IR/BasicBlock.h>

using namespace llvm;

bool getBlockDominators(BasicBlock &BB, std::map<BasicBlock*, std::set<BasicBlock*>> &dominators) {
    std::set<BasicBlock*> blockDoms;

    for (BasicBlock &B : *BB.getParent()) {
        blockDoms.insert(&B);
    }

    for (BasicBlock *pred : predecessors(&BB)) {
        std::set<BasicBlock*> res;
        std::set_intersection(
            blockDoms.begin(), blockDoms.end(),
            dominators[pred].begin(), dominators[pred].end(),
            std::inserter(res, res.begin())
        );

        blockDoms = std::move(res);
    }

    blockDoms.insert(&BB);

    if (blockDoms != dominators[&BB]) {
        dominators[&BB] = blockDoms;
        return true;
    }

    return false;
}

bool dominatorAnalysis(Function &F, std::map<BasicBlock*, std::set<BasicBlock*>> &blocksDom) {
    bool isChanged = false;

    for (BasicBlock &BB : F) {
        if (&BB == &F.getEntryBlock()) {
            if (blocksDom[&BB].empty()) {
                blocksDom[&BB] = {&BB};
                isChanged = true;
            }
        } else if (getBlockDominators(BB, blocksDom)) {
            isChanged = true;
        }
    }

    return isChanged;
}

PreservedAnalyses DominatorAnalysis::run(Module &M, ModuleAnalysisManager &AM) {
    FunctionAnalysisManager &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    // Run optimizations on each function in the module
    for (auto Fiter = M.begin(); Fiter != M.end(); ++Fiter) {
        AliasAnalysis &AA = FAM.getResult<AAManager>(*Fiter);
        std::map<BasicBlock*, std::set<BasicBlock*>> blocksDoms;
        while (dominatorAnalysis(*Fiter, blocksDoms)) {};

        outs() << "Dominators for function: " << Fiter->getName();
        outs() << "\n\n";

        for (auto &pair : blocksDoms) {
            outs() << "Dominators for basic block: " << pair.first->getName();
            outs() << "\n";

            for (auto &doms : pair.second) {
                outs() << doms->getName() << "\n";
            }
        }

        outs() << "------------------\n\n";
    }

    return PreservedAnalyses::all();
}


PassPluginLibraryInfo getDominatorAnalysisPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "Dominator Analysis", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // Register the pass with the pass builder
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) -> bool {
                    // Allow the pass to be invoked via -passes=reaching-definitions
                    if (Name == "dominator-analysis") {
                        MPM.addPass(DominatorAnalysis());
                        return true;
                    }
                    return false;
                });
        }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getDominatorAnalysisPluginInfo();
}
