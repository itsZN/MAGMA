#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/PassRegistry.h"

#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> CMD_LOW("magmanolow", cl::ZeroOrMore, cl::Hidden, cl::desc("MAGMA Low impact level"));
static cl::opt<bool> CMD_HIGH("magmanohigh", cl::ZeroOrMore, cl::Hidden, cl::desc("MAGMA High impact level"));
static cl::opt<bool> CMD_EXP("magmanoexp", cl::ZeroOrMore, cl::Hidden, cl::desc("MAGMA Generate exploit passes"));

/*
static cl::opt<bool> CMD_NOFMT("magmanofmt", cl::ZeroOrMore, cl::Hidden, cl::desc("MAGMA Generate exploit passes"));
static cl::opt<bool> CMD_NONFF("magmanonff", cl::ZeroOrMore, cl::Hidden, cl::desc("MAGMA Generate exploit passes"));
static cl::opt<bool> CMD_NOVOL("magmanovol", cl::ZeroOrMore, cl::Hidden, cl::desc("MAGMA Generate exploit passes"));
*/

namespace {
    Function * magma_strlen = NULL;
    Function * magma_gets = NULL;
    Function * magma_rgets = NULL;
    Function * magma_win = NULL;

    struct BufferSizePass: public InstVisitor<BufferSizePass> {
        BufferSizePass() {}

        virtual void visitAlloca(AllocaInst &I) {
            if (ArrayType* at = dyn_cast<ArrayType>(I.getAllocatedType())) {
                if (at->getNumElements()>1) {
                    //ArrayType* nt = ArrayType::get(at->getElementType(), (at->getNumElements()*90)/100);
                    //I.setAllocatedType(nt);
                }
            }
        }

        virtual void visitCallInst(CallInst &I) {
            if (I.getCalledFunction() && (I.getCalledFunction()->getName() == "malloc" )) {
                Value* op0 = I.getArgOperand(0);
                
                if (ConstantInt* i = dyn_cast<ConstantInt>(op0)) {
                    errs() << I << "\n";
                    errs() << *op0 << "\n";
                    errs() << i->getLimitedValue() << "\n";

                    I.setArgOperand(0, ConstantInt::get(op0->getType(),(i->getLimitedValue()*90)/100));
                }
                
            }
        }
    };

    struct NullFreeFinderPass: public FunctionPass {
        NullFreeFinderPass(char ID) : FunctionPass(ID) {}

        bool compareInstructionAsUsers(Instruction* a, Instruction* b, int limit=10) {
            if (limit == 0)
                return false;
            //errs() << "comparing " << *a << " to " << *b << "\n";
            if (a==b) {
                return true;
            }
            if (a->getOpcodeName() != b->getOpcodeName())
                return false;
            if (a->getNumOperands() != b->getNumOperands())
                return false;

            for (User::op_iterator i = a->op_begin(), j = b->op_begin();
                    i != a->op_end() && j != b->op_end(); i++, j++) {
                Instruction* ii = dyn_cast<Instruction>(*i);
                Instruction* ij = dyn_cast<Instruction>(*j);

                if (ii && ij) {
                    if (!compareInstructionAsUsers(ii, ij, limit-1))
                        return false;
                } else if (*i != *j)
                    return false;

            }
            return true;

        }

        bool runOnFunction(Function &F) override {
            std::vector<Instruction*> freed;
            std::vector<Instruction*> toRemove;

            for (auto &block : F) {
                for (auto &inst : block) {
                    if (CallInst* I = dyn_cast<CallInst>(&inst)) {

                        if (I->getCalledFunction() && I->getCalledFunction()->getName() == "free") {
                            //errs() << *I << "\n";
                            //errs() << *I->getArgOperand(0) << "\n";
                            Value* ptr = I->getArgOperand(0);
                            if (Instruction* bitcast = dyn_cast<Instruction>(ptr)) {
                                if (Instruction* load = dyn_cast<Instruction>(bitcast->getOperand(0))) {
                                    //errs() << "Freed " << *load->getOperand(0) << "\n";
                                    if (Instruction* realVal = dyn_cast<Instruction>(load->getOperand(0)))
                                        freed.push_back(realVal);
                                }
                            }
                        }
                    }
                    if (StoreInst* I = dyn_cast<StoreInst>(&inst)) {
                        //errs() << *I << "\n";
                        if (Instruction* realVal = dyn_cast<Instruction>(I->getOperand(1))) {
                            for (int i=0; i<freed.size(); i++) {
                                if (compareInstructionAsUsers(freed[i], realVal)) {
                                    freed.erase(find(freed.begin(), freed.end(), freed[i]));
                                    //errs() << *I->getOperand(0) << "\n";
                                    if (isa<ConstantPointerNull>(I->getOperand(0))) {
                                        //errs() << "NULL\n";
                                        toRemove.push_back(I);
                                    }

                                    break;
                                }

                            }
                        }
                    }
                }
            }
            //errs() << "got to here\n";
            for (int i=0; i< toRemove.size(); i++)
                toRemove[i]->eraseFromParent();
            return true;
        }
    };



    struct MemPermsPass : public InstVisitor<MemPermsPass> {
        MemPermsPass() {}

        virtual void visitCallInst(CallInst &I) {
            if (I.getCalledFunction() && (I.getCalledFunction()->getName() == "mmap" ||
                    I.getCalledFunction()->getName() == "__mmap" ||
                    I.getCalledFunction()->getName() == "mprotect")) {
                Value* op2 = I.getArgOperand(2);
                if (isa<ConstantInt>(op2)) {
                    I.setArgOperand(2, ConstantInt::get(op2->getType(),7));
                }
            }
        }
    };


    struct FmtPass : public InstVisitor<FmtPass> {
        FmtPass() {}

        virtual void visitCallInst(CallInst &I) {
            if (I.getCalledFunction() && (I.getCalledFunction()->getName() == "printf" || 
                        I.getCalledFunction()->getName() == "fprintf")) {
                int argoff = 0;
                if (I.getCalledFunction()->getName() == "fprintf")
                    argoff = 1;
                Value * op0 = I.getArgOperand(argoff)->stripPointerCasts();
                //errs() << I << "\n";


                if (isa<GlobalVariable>(op0) && isa<Constant>(op0)) {
                    GlobalVariable * g = dyn_cast<GlobalVariable>(op0);
                    StringRef val = dyn_cast<ConstantDataSequential>(g->getInitializer())->getAsCString();
                    //errs() << val << "\n";


                    bool found = false;
                    User::op_iterator opiter = I.op_begin()+1+argoff;

                    IRBuilder<> builder(&I);

                    size_t format;
                    while ((format = val.find("%s")) != StringRef::npos) {
                        found = true;
                        //errs() << "Contains %s! At " <<format << "\n";
                        StringRef s0 = val.slice(0,format);
                        //StringRef s1 = val.slice(format,format+2);
                        val = val.slice(format+2, StringRef::npos);
                        //errs() << s0 << " : " << s1 << " : " << val << "\n";

                        std::vector<Value*> args0;
                        if (argoff==1)
                            args0.push_back(I.getArgOperand(0));
                        args0.push_back(builder.CreateGlobalStringPtr(s0));

                        if (s0.size()>0) {
                            for (int i=0; i<s0.size()-1; i++) {
                                if (s0[i]=='%' && s0[i+1]!='%')
                                    args0.push_back(*(opiter++));
                            }
                            ArrayRef<Value*> args0_a(args0);
                            CallInst::Create(I.getCalledFunction(), args0_a, "", &I);
                        }
                        std::vector<Value*> args1;
                        if (argoff==1)
                            args1.push_back(I.getArgOperand(0));
                        args1.push_back(*(opiter++));
                        ArrayRef<Value*> args1_a(args1);
                        CallInst::Create(I.getCalledFunction(), args1_a, "", &I);
                    }
                    // Place the last printf if it needed to be split
                    if (found && val.size()>0) {
                        std::vector<Value*> args;
                        if (argoff==1)
                            args.push_back(I.getArgOperand(0));
                        args.push_back(builder.CreateGlobalStringPtr(val));
                        for(; opiter!=I.op_end(); opiter++)
                            args.push_back(*opiter);
                        ArrayRef<Value*> args_a(args);
                        CallInst* last = CallInst::Create(I.getCalledFunction(), args_a, "");
                        ReplaceInstWithInst(&I, last);
                    }
                }
            }

        }
    };

    struct GetsPass : public InstVisitor<GetsPass>
    {
        GetsPass() {}

        virtual void visitCallInst(CallInst &I)
        {
            Function * F = I.getCalledFunction();
            if (!F) return;

            StringRef fname = F->getName();
            if (fname == "fgets")
            {
                Value * op2 = I.getArgOperand(2)->stripPointerCasts();
                if (LoadInst * op2inst = dyn_cast<LoadInst>(op2)) {
                    if (op2inst->getPointerOperand()->stripPointerCasts()->getName() == "stdin")
                    {
                        Value * new_args[] = {I.getArgOperand(0)};
                        CallInst * c = CallInst::Create(magma_gets, ArrayRef<Value*>(new_args, 1), "");
                        ReplaceInstWithInst(&I, c);
                    }
                }
            }
            else if (fname == "read")
            {
                Value * op0 = I.getArgOperand(0)->stripPointerCasts();
                if (dyn_cast<ConstantInt>(op0) && dyn_cast<ConstantInt>(op0)->isZero())
                {
                    Value * new_args[] = {I.getArgOperand(1)};
                    if (magma_rgets==NULL) {
                        Module& M = *I.getModule();

                        Type * int8_type = Type::getInt8PtrTy(M.getContext());
                        Type * gets_args[] = {int8_type};
                        Type * int64_type = Type::getInt64Ty(M.getContext());

                        FunctionType * rgets_type = FunctionType::get(int64_type, ArrayRef<Type*>(gets_args, 1), 0);

                        StringRef funcName = "magma_rgets"+M.getName().str();
                        Constant * const_rgets_func = M.getOrInsertFunction(funcName, rgets_type);
                        magma_rgets = cast<Function>(const_rgets_func);

                        Value * rgets_args[] = {&*magma_rgets->arg_begin()};

                        BasicBlock * bb = BasicBlock::Create(M.getContext(), "entry", magma_rgets);
                        IRBuilder<> builder(bb);
                        builder.CreateCall(magma_gets, ArrayRef<Value*>(rgets_args, 1), "entry");
                        Value * gets_len = builder.CreateCall(magma_strlen, ArrayRef<Value*>(rgets_args, 1), "entry");
                        builder.CreateRet(gets_len);
                    }
                    CallInst * c = CallInst::Create(magma_rgets, ArrayRef<Value*>(new_args, 1), "");
                    ReplaceInstWithInst(&I, c);
                }
            }
        }
    };

    struct VolatilePass : public InstVisitor<VolatilePass>
    {
        VolatilePass() {}

        virtual void visitLoadInst(LoadInst & I) { if (I.isVolatile()) I.setVolatile(0); }
        virtual void visitStoreInst(StoreInst & I) { if (I.isVolatile()) I.setVolatile(0); }
        virtual void visitAtomicCmpXchgInst(AtomicCmpXchgInst &I) { if (I.isVolatile()) I.setVolatile(0); }
        virtual void visitMemIntrinsic(MemIntrinsic &I) { if (I.isVolatile()) I.setVolatile(0); }
    };

    struct OffByOnePass : public InstVisitor<OffByOnePass>
    {
        OffByOnePass() {}

        virtual void visitICmpInst(CmpInst & I)
        {
            CmpInst::Predicate pred = I.getPredicate();
            switch(pred)
            {
                case CmpInst::Predicate::ICMP_ULT:
                    pred = CmpInst::Predicate::ICMP_ULE;
                    break;
                case CmpInst::Predicate::ICMP_SLT:
                    pred = CmpInst::Predicate::ICMP_SLE;
                    break;
                default:
                    break;
            }

            I.setPredicate(pred);
        }
    };

    struct Magma: public FunctionPass {
        static char ID;
        Magma() : FunctionPass(ID) {}

        void remove_stack_canary(Function &F)
        {
            StringRef attr = "stack-protector-buffer-size";
            if (F.hasFnAttribute(attr)) F.addFnAttr(attr);
        }

        bool runOnFunction(Function &F ) override {

            bool low=(CMD_LOW.getNumOccurrences() == 0),
                high=(CMD_HIGH.getNumOccurrences() == 0),
                exp=(CMD_EXP.getNumOccurrences() == 0);
            bool nofmt=false, nonff=false, novol=false;
            bool nogets=false, nostr=false, noobo=true;
            bool nobuff=true, nomem=false, noremsc=false;
            
            
            if (low) {
                if (!nofmt) {
                    FmtPass fmt;
                    fmt.visit(F);
                }

                if (!nonff) {
                    NullFreeFinderPass nffp(ID);
                    nffp.runOnFunction(F);
                }

                if (!novol) {
                    VolatilePass vol;
                    vol.visit(F);
                }
            }


            if (high) {
                if (!nogets) {
                    GetsPass gets;
                    gets.visit(F);
                }

                if (!noobo) {
                    OffByOnePass off;
                    off.visit(F);
                }

                if (!nobuff) {
                    BufferSizePass bsp;
                    bsp.visit(F);
                }
            }

            if (exp) {
                if (!nomem) {
                    MemPermsPass mpp;
                    mpp.visit(F);
                }

                if (!noremsc) {
                    remove_stack_canary(F);
                }
            }
            
            



            return true;
        }
    };

    struct MagmaMod : public ModulePass
    {
        static char ID;
        MagmaMod() : ModulePass(ID) {}

        bool runOnModule(Module & M)
        {
            errs() << "Mname " << M.getName() << "\n";
            Type * int8_type = Type::getInt8PtrTy(M.getContext());
            Type * gets_args[] = {int8_type};
            FunctionType * gets_type = FunctionType::get(int8_type, ArrayRef<Type*>(gets_args, 1), 0);
            Constant * const_gets_func = M.getOrInsertFunction("gets", gets_type);
            magma_gets = cast<Function>(const_gets_func);

            Type * int64_type = Type::getInt64Ty(M.getContext());
            FunctionType * strlen_type = FunctionType::get(int64_type, ArrayRef<Type*>(gets_args, 1), 0);
            Constant * const_strlen_func = M.getOrInsertFunction("strlen", strlen_type);
            magma_strlen = cast<Function>(const_strlen_func);

            if (CMD_EXP.getNumOccurrences() == 0) {
                if (magma_win==NULL) {
                    Constant * const_system = M.getOrInsertFunction("system", strlen_type);
                    Function * magma_system = cast<Function>(const_system);


                    Type * win_args[] = {int8_type};
                    FunctionType * win_type = FunctionType::get(int64_type, ArrayRef<Type*>(win_args,1), 0);
                    StringRef funcName = "magma_win"+M.getName().str();
                    Constant * const_win = M.getOrInsertFunction(funcName, win_type);
                    magma_win = cast<Function>(const_win);

                    BasicBlock * bb = BasicBlock::Create(M.getContext(), "entry", magma_win);
                    IRBuilder<> builder(bb);
                    Value * binsh = builder.CreateGlobalStringPtr("/bin/sh", ".str");
                    Value * system_args[] = {binsh};
                    Value * gets_len = builder.CreateCall(magma_system, ArrayRef<Value*>(system_args, 1), "entry");
                    builder.CreateRet(gets_len);
                }
            }
            
            Magma fp;
            for (Function & F : M) fp.runOnFunction(F);
            

            return true;
        }

    };
}

char Magma::ID = 0;
char MagmaMod::ID = 0;
static RegisterPass<MagmaMod> X("magma", "magma pass", false, false);

static void loadPass(const PassManagerBuilder &Builder, legacy::PassManagerBase &PM) {
      PM.add(new MagmaMod());
}
// These constructors add our pass to a list of global extensions.
static RegisterStandardPasses clangtoolLoader_Ox(PassManagerBuilder::EP_OptimizerLast, loadPass);
static RegisterStandardPasses clangtoolLoader_O0(PassManagerBuilder::EP_EnabledOnOptLevel0, loadPass);

