//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/StoredValueRef.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/ValuePrinter.h"
#include "cling/Utils/AST.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/IR/Module.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Frontend/CompilerInstance.h"

using namespace cling;
using namespace clang;
using namespace llvm;

StoredValueRef::StoredValue::StoredValue(Interpreter& interp,
                                         QualType clangTy)
  : Value(GenericValue(), clangTy), m_Interp(interp), m_Mem(0){
  if (clangTy->isIntegralOrEnumerationType() ||
      clangTy->isRealFloatingType() ||
      clangTy->hasPointerRepresentation()) {
    return;
  };
  if (const MemberPointerType* MPT = clangTy->getAs<MemberPointerType>()) {
    if (MPT->isMemberDataPointer()) {
      return;
    }
  }
  m_Mem = m_Buf;
  const uint64_t size = (uint64_t) getAllocSizeInBytes();
  if (size > sizeof(m_Buf)) {
    m_Mem = new char[size];
  }
  setGV(llvm::PTOGV(m_Mem));
}

StoredValueRef::StoredValue::~StoredValue() {
  // Destruct the object, then delete the memory if needed.
  Destruct();
  if (m_Mem != m_Buf)
    delete [] m_Mem;
}

void* StoredValueRef::StoredValue::GetDtorWrapperPtr(CXXRecordDecl* CXXRD) {
   std::string funcname;
  {
    llvm::raw_string_ostream namestr(funcname);
    namestr << "__cling_StoredValue_Destruct_" << CXXRD;
  }

  std::string code("extern \"C\" void ");
  {
    std::string typeName
      = utils::TypeName::GetFullyQualifiedName(getClangType(),
                                               CXXRD->getASTContext());
    std::string dtorName = CXXRD->getNameAsString();
    code += funcname + "(void* obj){((" + typeName + "*)obj)->~"
      + dtorName + "();}";
  }

  return m_Interp.compileFunction(funcname, code, true /*ifUniq*/,
                                  false /*withAccessControl*/);
}

void StoredValueRef::StoredValue::Destruct() {
  // If applicable, call addr->~Type() to destruct the object.
  // template <typename T> void destr(T* obj = 0) { (T)obj->~T(); }
  // |-FunctionDecl destr 'void (struct XB *)'
  // |-TemplateArgument type 'struct XB'
  // |-ParmVarDecl obj 'struct XB *'
  // `-CompoundStmt
  //   `-CXXMemberCallExpr 'void'
  //     `-MemberExpr '<bound member function type>' ->~XB
  //       `-ImplicitCastExpr 'struct XB *' <LValueToRValue>
  //         `-DeclRefExpr  'struct XB *' lvalue ParmVar 'obj' 'struct XB *'

  QualType QT = getClangType();
  const RecordType* RT = 0;
  // Find the underlying record type.
  while (!RT) {
    if (QT.isConstQualified())
      return;
    const clang::Type* Ty = QT.getTypePtr();
    RT = dyn_cast<RecordType>(Ty);
    if (!RT) {
      const ElaboratedType* ET = 0;
      const TemplateSpecializationType* TT = 0;
      if ((ET = dyn_cast<ElaboratedType>(Ty)))
        QT = ET->getNamedType();
      else if ((TT = dyn_cast<TemplateSpecializationType>(Ty)))
        QT = TT->desugar();
      if (!RT && !ET && !TT)
        return;
    }
  }
  CXXRecordDecl* CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl());
  // Freeing will happen either way; construction only exists for RecordDecls.
  // And it's only worth calling it for non-trivial d'tors. But without
  // synthesizing AST nodes we can only invoke the d'tor for named decls.
  if (!CXXRD || CXXRD->hasTrivialDestructor() || !CXXRD->getDeclName())
    return;

  CXXRD = CXXRD->getCanonicalDecl();
  void* funcPtr = GetDtorWrapperPtr(CXXRD);
  if (!funcPtr)
    return;

  typedef void (*DtorWrapperFunc_t)(void* obj);
  // Go via void** to avoid fun-cast warning:
  DtorWrapperFunc_t wrapperFuncPtr = *(DtorWrapperFunc_t*) &funcPtr;
  (*wrapperFuncPtr)(getAs<void*>());
}

long long StoredValueRef::StoredValue::getAllocSizeInBytes() const {
  const ASTContext& ctx = m_Interp.getCI()->getASTContext();
  return (long long) ctx.getTypeSizeInChars(getClangType()).getQuantity();
}


void StoredValueRef::dump() const {
  ASTContext& ctx = m_Value->m_Interp.getCI()->getASTContext();
  valuePrinterInternal::StreamStoredValueRef(llvm::errs(), this, ctx);
}

StoredValueRef StoredValueRef::allocate(Interpreter& interp, QualType t) {
  return new StoredValue(interp, t);
}

StoredValueRef StoredValueRef::bitwiseCopy(Interpreter& interp,
                                           const cling::Value& value) {
  StoredValue* SValue 
    = new StoredValue(interp, value.getClangType());
  if (SValue->m_Mem) {
    const char* src = (const char*)value.getGV().PointerVal;
    // It's not a pointer. LLVM stores a char[5] (i.e. 5 x i8) as an i40,
    // so use that instead. We don't keep it as an int; instead, we "allocate"
    // it as a "proper" char[5] in the m_Mem. "Allocate" because it uses the
    // m_Buf, so no actual allocation happens.
    uint64_t IntVal = value.getGV().IntVal.getSExtValue();
    if (!src) src = (const char*)&IntVal;
    memcpy(SValue->m_Mem, src,
           SValue->getAllocSizeInBytes());
  } else {
    SValue->setGV(value.getGV());
  }
  return SValue;
}
