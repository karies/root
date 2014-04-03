//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "ValueExtractionSynthesizer.h"

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"

#include "cling/Utils/AST.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"

using namespace clang;

namespace {
  static
  bool HasAccessibleCopyConstructor(const CXXRecordDecl* CXXRD, void* UserData){
    Sema* S = (Sema*)UserData;
    // Check this class's copy constructor.
    if (CXXConstructorDecl* Ctor
        = S->LookupCopyingConstructor(const_cast<CXXRecordDecl*>(CXXRD), 0)){
      if (Ctor->isInvalidDecl()) return false;
      if (Ctor->getAccess() != AS_public && Ctor->getAccess() != AS_none)
        return false;
    }
    // Check the class's copy constructor of all bases.
    return CXXRD->forallBases(&HasAccessibleCopyConstructor, UserData,
                              true /*AllowShortCircuit*/);
  }
}

namespace cling {
  ValueExtractionSynthesizer::ValueExtractionSynthesizer(clang::Sema* S)
    : TransactionTransformer(S), m_Context(&S->getASTContext()), m_gClingVD(0),
      m_UnresolvedNoAlloc(0), m_UnresolvedWithAlloc(0),
      m_UnresolvedCopyArray(0) { }

  // pin the vtable here.
  ValueExtractionSynthesizer::~ValueExtractionSynthesizer() { }

  namespace {
    class ReturnStmtCollector : public StmtVisitor<ReturnStmtCollector> {
    private:
      llvm::SmallVectorImpl<Stmt**>& m_Stmts;
    public:
      ReturnStmtCollector(llvm::SmallVectorImpl<Stmt**>& S)
        : m_Stmts(S) {}

      void VisitStmt(Stmt* S) {
        for(Stmt::child_iterator I = S->child_begin(), E = S->child_end();
            I != E; ++I) {
          if (!*I)
            continue;
          Visit(*I);
          if (isa<ReturnStmt>(*I))
            m_Stmts.push_back(&*I);
        }
      }
    };
  }

  void ValueExtractionSynthesizer::Transform() {
    if (!getTransaction()->getCompilationOpts().ResultEvaluation)
      return;

    FunctionDecl* FD = getTransaction()->getWrapperFD();

    int foundAtPos = -1;
    Expr* lastExpr = utils::Analyze::GetOrCreateLastExpr(FD, &foundAtPos,
                                                         /*omitDS*/false,
                                                         m_Sema);
    if (foundAtPos < 0)
      return;

    typedef llvm::SmallVector<Stmt**, 4> StmtIters;
    StmtIters returnStmts;
    ReturnStmtCollector collector(returnStmts);
    CompoundStmt* CS = cast<CompoundStmt>(FD->getBody());
    collector.VisitStmt(CS);

    if (isa<Expr>(*(CS->body_begin() + foundAtPos)))
      returnStmts.push_back(CS->body_begin() + foundAtPos);

    // We want to support cases such as:
    // gCling->evaluate("if() return 'A' else return 12", V), that puts in V,
    // either A or 12.
    // In this case the void wrapper is compiled with the stmts returning
    // values. Sema would cast them to void, but the code will still be
    // executed. For example:
    // int g(); void f () { return g(); } will still call g().
    //
    for (StmtIters::iterator I = returnStmts.begin(), E = returnStmts.end();
         I != E; ++I) {
      ReturnStmt* RS = dyn_cast<ReturnStmt>(**I);
      if (RS) {
        if (Expr* RetV = RS->getRetValue()) {
          assert (RetV->getType()->isVoidType() && "Must be void type.");
          // Any return statement will have been "healed" by Sema
          // to correspond to the original void return type of the
          // wrapper, using a ImplicitCastExpr 'void' <ToVoid>.
          // Remove that.
          if (ImplicitCastExpr* VoidCast = dyn_cast<ImplicitCastExpr>(RetV)) {
            lastExpr = VoidCast->getSubExpr();
          }
        }
      }
      else
        lastExpr = cast<Expr>(**I);

      if (lastExpr) {
        QualType lastExprTy = lastExpr->getType();
        // May happen on auto types which resolve to dependent.
        if (lastExprTy->isDependentType())
          continue;
        // Set up lastExpr properly.
        // Change the void function's return type
        // We can't PushDeclContext, because we don't have scope.
        Sema::ContextRAII pushedDC(*m_Sema, FD);

        if (lastExprTy->isFunctionType()) {
          // A return type of function needs to be converted to
          // pointer to function.
          lastExprTy = m_Context->getPointerType(lastExprTy);
          lastExpr = m_Sema->ImpCastExprToType(lastExpr, lastExprTy,
                                               CK_FunctionToPointerDecay,
                                               VK_RValue).take();
        }

        //
        // Here we don't want to depend on the JIT runFunction, because of its
        // limitations, when it comes to return value handling. There it is
        // not clear who provides the storage and who cleans it up in a
        // platform independent way.
        //
        // Depending on the type we need to synthesize a call to cling:
        // 0) void : set the value's type to void;
        // 1) enum, integral, float, double, referece, pointer types :
        //      call to cling::internal::setValueNoAlloc(...);
        // 2) object type (alloc on the stack) :
        //      cling::internal::setValueWithAlloc
        //   2.1) constant arrays:
        //          call to cling::runtime::internal::copyArray(...)
        //
        // We need to synthesize later:
        // Wrapper has signature: void w(cling::Value SVR)
        // case 1):
        //   setValueNoAlloc(gCling, &SVR, lastExprTy, lastExpr())
        // case 2):
        //   new (setValueWithAlloc(gCling, &SVR, lastExprTy)) (lastExpr)
        // case 2.1):
        //   copyArray(src, placement, size)

        if (!m_gClingVD)
          FindAndCacheRuntimeDecls();

        Expr* SVRInit = SynthesizeSVRInit(lastExpr);
        // if we had return stmt update to execute the SVR init, even if the
        // wrapper returns void.
        if (RS) {
          Expr* retValue = RS->getRetValue();
          if (!retValue)
            RS->setRetValue(SVRInit);
          else if (ImplicitCastExpr* VoidCast
                   = dyn_cast<ImplicitCastExpr>(retValue))
            VoidCast->setSubExpr(SVRInit);
        }
        else
          **I = SVRInit;
      }
    }
  }

  Expr* ValueExtractionSynthesizer::SynthesizeSVRInit(Expr* E) const {

    QualType ETy = E->getType();

    // The expr result is transported as reference, pointer, array, float etc
    // based on the desugared type. We should still expose the typedef'ed
    // (sugared) type to the cling::Value.
    QualType desugaredTy = ETy.getDesugaredType(*m_Context);

    bool isValidValue = true;
    if (const RecordType* RT = dyn_cast<RecordType>(desugaredTy)) {
      if (CXXRecordDecl* CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl()))
        isValidValue = HasAccessibleCopyConstructor(CXXRD, m_Sema);

      if (!isa<ExprWithCleanups>(E)) {
        // returning a lvalue (not a temporary): the value should contain
        // a reference to the lvalue instead of copying it.
        desugaredTy = m_Context->getLValueReferenceType(desugaredTy);
        ETy = m_Context->getLValueReferenceType(ETy);
      }
    }

    // Build a reference to gCling
    ExprResult gClingDRE
      = m_Sema->BuildDeclRefExpr(m_gClingVD, m_Context->VoidPtrTy,
                                 VK_RValue, SourceLocation());
    // We have the wrapper as Sema's CurContext
    FunctionDecl* FD = cast<FunctionDecl>(m_Sema->CurContext);

    // Build a reference to Value* in the wrapper, should be
    // the only argument of the wrapper.
    ExprResult wrapperSVRDRE
      = m_Sema->BuildDeclRefExpr(FD->getParamDecl(0), m_Context->VoidPtrTy,
                                 VK_RValue, E->getLocStart());
    uint64_t ETyUInt64 = isValidValue ? (uint64_t)ETy.getAsOpaquePtr() : 0;
    Expr* ETyVP
      = utils::Synthesize::CStyleCastPtrExpr(m_Sema, m_Context->VoidPtrTy,
                                             ETyUInt64);

    llvm::SmallVector<Expr*, 4> CallArgs;
    CallArgs.push_back(gClingDRE.take());
    CallArgs.push_back(wrapperSVRDRE.take());
    CallArgs.push_back(ETyVP);

    ExprResult Call;
    SourceLocation noLoc;
    if (!isValidValue) {
      // Need to set the value to invalid i.e. 0 type.
      // We need to synthesize setValueNoAlloc(...), E, because we still need
      // to run E.
      Call = m_Sema->ActOnCallExpr(/*Scope*/0, m_UnresolvedNoAlloc,
                                   E->getLocStart(), CallArgs,
                                   E->getLocEnd());
      Call = m_Sema->CreateBuiltinBinOp(Call.get()->getLocStart(), BO_Comma,
                                        Call.take(), E);
    }
    else if (desugaredTy->isVoidType()) {
      // In cases where the cling::Value gets reused we need to reset the
      // previous settings to void.
      // We need to synthesize setValueNoAlloc(...), E, because we still need
      // to run E.
      Call = m_Sema->ActOnCallExpr(/*Scope*/0, m_UnresolvedNoAlloc,
                                   E->getLocStart(), CallArgs,
                                   E->getLocEnd());
      Call = m_Sema->CreateBuiltinBinOp(Call.get()->getLocStart(), BO_Comma,
                                        Call.take(), E);

    }
    else if (desugaredTy->isRecordType() || desugaredTy->isConstantArrayType()){
      // 2) object types :
      // call new (setValueWithAlloc(gCling, &SVR, ETy)) (E)
      Call = m_Sema->ActOnCallExpr(/*Scope*/0, m_UnresolvedWithAlloc,
                                   E->getLocStart(), CallArgs,
                                   E->getLocEnd());
      Expr* placement = Call.take();
      if (const ConstantArrayType* constArray
          = dyn_cast<ConstantArrayType>(desugaredTy.getTypePtr())) {
        CallArgs.clear();
        CallArgs.push_back(E);
        CallArgs.push_back(placement);
        uint64_t arrSize
          = m_Context->getConstantArrayElementCount(constArray);
        Expr* arrSizeExpr
          = utils::Synthesize::IntegerLiteralExpr(*m_Context, arrSize);

        CallArgs.push_back(arrSizeExpr);
        // 2.1) arrays:
        // call copyArray(T* src, void* placement, int size)
        Call = m_Sema->ActOnCallExpr(/*Scope*/0, m_UnresolvedCopyArray,
                                     E->getLocStart(), CallArgs,
                                     E->getLocEnd());

      }
      else {
        TypeSourceInfo* ETSI
          = m_Context->getTrivialTypeSourceInfo(ETy, noLoc);

        Call = m_Sema->BuildCXXNew(E->getSourceRange(),
                                   /*useGlobal ::*/true,
                                   /*placementLParen*/ noLoc,
                                   MultiExprArg(placement),
                                   /*placementRParen*/ noLoc,
                                   /*TypeIdParens*/ SourceRange(),
                                   /*allocType*/ ETSI->getType(),
                                   /*allocTypeInfo*/ETSI,
                                   /*arraySize*/0,
                                   /*directInitRange*/E->getSourceRange(),
                                   /*initializer*/E,
                                   /*mayContainAuto*/false
                                   );
      }
    }
    else if (desugaredTy->isIntegralOrEnumerationType()
             || desugaredTy->isReferenceType()
             || desugaredTy->isPointerType()
             || desugaredTy->isFloatingType()) {
      if (desugaredTy->isIntegralOrEnumerationType()) {
        // 1)  enum, integral, float, double, referece, pointer types :
        //      call to cling::internal::setValueNoAlloc(...);

        // If the type is enum or integral we need to force-cast it into
        // uint64 in order to pick up the correct overload.
        if (desugaredTy->isIntegralOrEnumerationType()) {
          QualType UInt64Ty = m_Context->UnsignedLongLongTy;
          TypeSourceInfo* TSI
            = m_Context->getTrivialTypeSourceInfo(UInt64Ty, noLoc);
          Expr* castedE
            = m_Sema->BuildCStyleCastExpr(noLoc, TSI, noLoc, E).take();
          CallArgs.push_back(castedE);
        }
      }
      else if (desugaredTy->isReferenceType()) {
        // we need to get the address of the references
        Expr* AddrOfE = m_Sema->BuildUnaryOp(/*Scope*/0, noLoc, UO_AddrOf,
                                             E).take();
        CallArgs.push_back(AddrOfE);
      }
      else if (desugaredTy->isPointerType()) {
        // function pointers need explicit void* cast.
        QualType VoidPtrTy = m_Context->VoidPtrTy;
        TypeSourceInfo* TSI
          = m_Context->getTrivialTypeSourceInfo(VoidPtrTy, noLoc);
        Expr* castedE
          = m_Sema->BuildCStyleCastExpr(noLoc, TSI, noLoc, E).take();
        CallArgs.push_back(castedE);
      }
      else if (desugaredTy->isFloatingType()) {
        // floats and double will fall naturally in the correct
        // case, because of the overload resolution.
        CallArgs.push_back(E);
      }
      Call = m_Sema->ActOnCallExpr(/*Scope*/0, m_UnresolvedNoAlloc,
                                   E->getLocStart(), CallArgs,
                                   E->getLocEnd());
    }
    else
      assert(0 && "Unhandled code path?");

    assert(!Call.isInvalid() && "Invalid Call");
    return Call.take();
  }

  void ValueExtractionSynthesizer::FindAndCacheRuntimeDecls() {
    assert(!m_gClingVD && "Called multiple times!?");
    NamespaceDecl* NSD = utils::Lookup::Namespace(m_Sema, "cling");
    NSD = utils::Lookup::Namespace(m_Sema, "runtime", NSD);
    m_gClingVD = cast<VarDecl>(utils::Lookup::Named(m_Sema, "gCling", NSD));
    NSD = utils::Lookup::Namespace(m_Sema, "internal",NSD);

    LookupResult R(*m_Sema, &m_Context->Idents.get("setValueNoAlloc"),
                   SourceLocation(), Sema::LookupOrdinaryName,
                   Sema::ForRedeclaration);

    m_Sema->LookupQualifiedName(R, NSD);
    assert(!R.empty()
           && "Cannot find cling::runtime::internal::setValueNoAlloc");

    CXXScopeSpec CSS;
    m_UnresolvedNoAlloc
      = m_Sema->BuildDeclarationNameExpr(CSS, R, /*ADL*/ false).take();

    R.clear();
    R.setLookupName(&m_Context->Idents.get("setValueWithAlloc"));
    m_Sema->LookupQualifiedName(R, NSD);
    assert(!R.empty()
           && "Cannot find cling::runtime::internal::setValueWithAlloc");
    m_UnresolvedWithAlloc
      = m_Sema->BuildDeclarationNameExpr(CSS, R, /*ADL*/ false).take();

    R.clear();
    R.setLookupName(&m_Context->Idents.get("copyArray"));
    m_Sema->LookupQualifiedName(R, NSD);
    assert(!R.empty() && "Cannot find cling::runtime::internal::copyArray");
    m_UnresolvedCopyArray
      = m_Sema->BuildDeclarationNameExpr(CSS, R, /*ADL*/ false).take();
  }
} // end namespace cling


// Provide implementation of the functions that ValueExtractionSynthesizer calls
namespace {
  ///\brief Allocate the Value and return the Value
  /// for an expression evaluated at the prompt.
  ///
  ///\param [in] interp - The cling::Interpreter to allocate the SToredValueRef.
  ///\param [in] vpQT - The opaque ptr for the clang::QualType of value stored.
  ///\param [out] vpStoredValRef - The Value that is allocated.
  static cling::Value&
  allocateStoredRefValueAndGetGV(void* vpI, void* vpSVR, void* vpQT) {
    cling::Interpreter* i = (cling::Interpreter*)vpI;
    clang::QualType QT = clang::QualType::getFromOpaquePtr(vpQT);
    cling::Value& SVR = *(cling::Value*)vpSVR;
    // Here the copy keeps the refcounted value alive.
    SVR = cling::Value(QT, i);
    return SVR;
  }
}
namespace cling {
namespace runtime {
  namespace internal {
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT) {
      // In cases of void we 'just' need to change the type of the value.
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT);
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, float value) {
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<float>() = value;
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, double value) {
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<double>() = value;
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT,
                         long double value) {
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<long double>()
        = value;
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT,
                         unsigned long long value) {
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT)
        .getAs<unsigned long long>() = value;
    }
    void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, const void* value){
      allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<void*>()
        = const_cast<void*>(value);
    }
    void* setValueWithAlloc(void* vpI, void* vpSVR, void* vpQT) {
      return allocateStoredRefValueAndGetGV(vpI, vpSVR, vpQT).getAs<void*>();
    }
  } // end namespace internal
} // end namespace runtime
} // end namespace cling
