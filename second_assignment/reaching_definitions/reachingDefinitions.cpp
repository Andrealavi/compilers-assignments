#include "reachingDefinitions.hpp"

using namespace llvm;

void removeKilled(std::map<StoreInst*, bool> &defsMap, Value *pointer, AliasAnalysis &AA) {
    for (const auto& pair : defsMap) {
        if (AA.alias(pair.first->getPointerOperand(), pointer) != AliasResult::NoAlias) {
            defsMap[pair.first] = false;
        }
    }
}

bool bbReachingDefs(std::map<BasicBlock*, std::map<StoreInst*, bool>> &reachDefs, BasicBlock &BB, AliasAnalysis &AA) {
    bool transformed = false;

    std::map<StoreInst*, bool> defsMap;

    if (reachDefs.find(&BB) == reachDefs.end()) {
        transformed = true;
        defsMap = {};
    } else {
        defsMap = reachDefs[&BB];
    }

    for (BasicBlock *pred : predecessors(&BB)) {
        for (auto &pair : reachDefs[pred]) {
            if (defsMap.find(pair.first) == defsMap.end() || pair.second == true) {
                defsMap[pair.first] = pair.second;
                transformed = true;
            }
        }
    }

    for (Instruction &inst : BB) {
        if (StoreInst *SI = dyn_cast<StoreInst>(&inst)) {
            if (defsMap.find(SI) == defsMap.end()) {
                Value *pointerOperand = SI->getPointerOperand();

                removeKilled(defsMap, pointerOperand, AA);
                defsMap[SI] = true;
            }
        }
    }

    if (transformed) reachDefs[&BB] = defsMap;

    return false;
}

bool reachingDefinitions(Function &F, std::map<BasicBlock*, std::map<StoreInst*, bool>> &blocksReachDefs, AliasAnalysis &AA) {
    bool transformed = false;

    for (BasicBlock &BB: F) {
       if (bbReachingDefs(blocksReachDefs, BB, AA)) transformed = true;
    }

    return transformed;
}

PreservedAnalyses ReachingDefinitions::run(Module &M, ModuleAnalysisManager &AM) {
    FunctionAnalysisManager &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    // Run optimizations on each function in the module
    for (auto Fiter = M.begin(); Fiter != M.end(); ++Fiter) {
        AliasAnalysis &AA = FAM.getResult<AAManager>(*Fiter);
        std::map<BasicBlock*, std::map<StoreInst*, bool>> blocksReachDefs;
        while (reachingDefinitions(*Fiter, blocksReachDefs, AA)) {};

        for (auto &pair : blocksReachDefs) {
            outs() << "Reaching definitions for basic block: " << pair.first->getName();
            //pair.first->print(outs());
            outs() << "\n";

            for (auto &def : pair.second) {
                def.first->print(outs());
                outs() << "\t" << def.second << "\n";
            }
        }
    }

    return PreservedAnalyses::all();
}


PassPluginLibraryInfo getReachingDefinitionsPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "Reaching Definitions", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // Register the pass with the pass builder
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) -> bool {
                    // Allow the pass to be invoked via -passes=reaching-definitions
                    if (Name == "reaching-definitions") {
                        MPM.addPass(ReachingDefinitions());
                        return true;
                    }
                    return false;
                });
        }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getReachingDefinitionsPluginInfo();
}
