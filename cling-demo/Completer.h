#include <iostream>
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Basic/LangOptions.h"
//#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/Preprocessor.h"
//#include "clang/Lex/Token.h"

#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/CodeCompleteOptions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "cling/Interpreter/Transaction.h"
#include "clang/Sema/Sema.h"

class Completer : public clang::PrintingCodeCompleteConsumer {

  cling::Interpreter *m_interp;
  cling::Transaction::IssuedDiags m_diags;
  clang::CompilerInstance *m_compilerInst;
  public:
  
  Completer(const CodeCompleteOptions &CodeCompleteOpts, cling::Interpreter *interp, clang::CompilerInstance *ci):
    PrintingCodeCompleteConsumer(CodeCompleteOpts,llvm::outs()){
    m_interp = interp;
    m_compilerInst = ci;
  }

  void setTransaction(cling::Transaction *&tr);

  void ProcessCodeCompleteResults(clang::Sema &S, clang::CodeCompletionContext Context,
                                  clang::CodeCompletionResult *Results,
                                  unsigned NumResults);

};

void Completer::setTransaction(cling::Transaction *&tr){
  //T = tr;
}
void Completer::ProcessCodeCompleteResults(clang::Sema &S,
                                           clang::CodeCompletionContext Context,
                                           clang::CodeCompletionResult *Results,
                                           unsigned NumResults){
  clang::PrintingCodeCompleteConsumer::ProcessCodeCompleteResults(S, Context, Results, NumResults);
  const cling::Transaction * transaction = m_interp->getCurrentTransaction();
  cling::Transaction *nonconst_transaction = const_cast<cling::Transaction *>(transaction);
  nonconst_transaction->setIssuedDiags(cling::Transaction::kErrors);

  //m_compilerInst->getPreprocessor().Lex(const_cast<clang::Token&>(m_interp->getParser().getCurToken()));
 // m_interp->getParser().SkipUntil(clang::tok::eof);
}