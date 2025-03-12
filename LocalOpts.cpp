#include "LocalOpts.h"
#include <iostream>


using namespace llvm;

bool runOnBasicBlock(BasicBlock &B) {

    // Preleviamo le prime due istruzioni del BB
    Instruction &Inst1st = *B.begin(), &Inst2nd = *(++B.begin());

    // L'indirizzo della prima istruzione deve essere uguale a quello del
    // primo operando della seconda istruzione (per costruzione dell'esempio)
    assert(&Inst1st == Inst2nd.getOperand(0));

    // Stampa la prima istruzione
    outs() << "PRIMA ISTRUZIONE: " << Inst1st << "\n";
    // Stampa la prima istruzione come operando
    outs() << "COME OPERANDO: ";
    Inst1st.printAsOperand(outs(), false);
    outs() << "\n";

    // User-->Use-->Value
    outs() << "I MIEI OPERANDI SONO:\n";
    for (auto *Iter = Inst1st.op_begin(); Iter != Inst1st.op_end(); ++Iter) {
        Value *Operand = *Iter;

        if (Argument *Arg = dyn_cast<Argument>(Operand)) {
            outs() << "\t" << *Arg << ": SONO L'ARGOMENTO N. " << Arg->getArgNo()
             <<" DELLA FUNZIONE " << Arg->getParent()->getName()
                            << "\n";
        }
        if (ConstantInt *C = dyn_cast<ConstantInt>(Operand)) {
            outs() << "\t" << *C << ": SONO UNA COSTANTE INTERA DI VALORE " << C->getValue()
                            << "\n";
        }
    }

    outs() << "LA LISTA DEI MIEI USERS:\n";
    for (auto Iter = Inst1st.user_begin(); Iter != Inst1st.user_end(); ++Iter) {
        outs() << "\t" << *(dyn_cast<Instruction>(*Iter)) << "\n";
    }

    outs() << "E DEI MIEI USI (CHE E' LA STESSA):\n";
    for (auto Iter = Inst1st.use_begin(); Iter != Inst1st.use_end(); ++Iter) {
        outs() << "\t" << *(dyn_cast<Instruction>(Iter->getUser())) << "\n";
    }

    // Manipolazione delle istruzioni
    Instruction *NewInst = BinaryOperator::Create(
            Instruction::Add, Inst1st.getOperand(0), Inst1st.getOperand(0));

    NewInst->insertAfter(&Inst1st);
    // Si possono aggiornare le singole references separatamente?
    // Controlla la documentazione e prova a rispondere.
    Inst1st.replaceAllUsesWith(NewInst);

    return true;
}

bool algebraicIdentityOptimization(Instruction &Inst) {
    if (Inst.isBinaryOp()) {
        Value *op1 = Inst.getOperand(0);
        Value *op2 = Inst.getOperand(1);

        unsigned int opCode = Inst.getOpcode();

        Value *newValue = nullptr;

        if (op1 == op2) {
            switch (opCode) {
                case Instruction::Sub:
                    newValue = Constant::getNullValue(Inst.getType());
                break;

                case Instruction::SDiv:
                    newValue = ConstantInt::get(Inst.getType(), 1);
                break;

                case Instruction::UDiv:
                    newValue = ConstantInt::get(Inst.getType(), 1);
                break;

                case Instruction::And:
                    newValue = op1;
                break;

                case Instruction::Or:
                    newValue = op1;
                break;

                default:
                break;
            }
        } else if (Value *constant = isa<Constant>(op1) ? op1 : isa<Constant>(op2) ? op2 : nullptr) {
            if (!constant) {
                return false;
            }

            Value *variable = constant == op1 ? op2 : op1;

            unsigned int constantValue = constant->getValueID();

            if (constantValue == 0) {
                switch (opCode) {
                    case Instruction::Add:
                        newValue = variable;
                    break;

                    case Instruction::Sub:
                        newValue = variable;
                    break;

                    case Instruction::Mul:
                        newValue = Constant::getNullValue(Inst.getType());
                    break;

                    case Instruction::Shl:
                        newValue = variable;
                    break;

                    case Instruction::LShr:
                        newValue = variable;
                    break;

                    default:
                    break;
                }
            } else if (constantValue == 1) {
                switch (opCode) {
                    case Instruction::Mul:
                        newValue = variable;
                    break;

                    case Instruction::UDiv:
                        newValue = variable;
                    break;

                    case Instruction::SDiv:
                        newValue = variable;
                    break;

                    default:
                    break;
                }
            }
        }

        if (newValue) {
            Inst.replaceAllUsesWith(newValue);

            Inst.eraseFromParent();

            return true;
        }

    }

    return false;
}

bool runOnBBOptimizations(BasicBlock &BB) {

    for (Instruction &Inst : BB) {
        algebraicIdentityOptimization(Inst);
    }

    return true;
};

bool runOnFunction(Function &F) {
    bool Transformed = false;

    for (auto Iter = F.begin(); Iter != F.end(); ++Iter) {
        if (runOnBBOptimizations(*Iter)) {
            Transformed = true;
        }
    }

    return Transformed;
}

PreservedAnalyses LocalOpts::run(Module &M, ModuleAnalysisManager &AM) {
    for (auto Fiter = M.begin(); Fiter != M.end(); ++Fiter)
        if (runOnFunction(*Fiter))
            return PreservedAnalyses::none();

    return PreservedAnalyses::all();
};

PassPluginLibraryInfo getLocalOptsPluginInfo() {
return {LLVM_PLUGIN_API_VERSION, "LocalOpts", LLVM_VERSION_STRING,
                [](PassBuilder &PB) {
                    PB.registerPipelineParsingCallback(
                            [](StringRef Name, ModulePassManager &MPM,
                                    ArrayRef<PassBuilder::PipelineElement>) -> bool {
                                if (Name == "local-opts") {
                                    MPM.addPass(LocalOpts());
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
    return getLocalOptsPluginInfo();
}
