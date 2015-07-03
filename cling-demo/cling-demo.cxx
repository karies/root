#include "Completer.h"

#include <string>
#include <vector>
#include <sstream>
#include <iostream>


void simple(cling::Interpreter& interp) {
  // Compile a function (and possibly some other code) and get a fairly raw
  // function pointer to it.
  void* funPtrVoid
    = interp.compileFunction("MyFunc",
                             "extern \"C\" double MyFunc(int v) {"
                             "  return v + 0.42; }");

  // We know its type.
  typedef double (*fun_t)(int);
  fun_t funPtr = (fun_t)funPtrVoid;
  std::cout << "Got a " << (*funPtr)(12) << std::endl;
}


void less_simple(cling::Interpreter& interp) {
  // Inject some code into the interpreter.
  interp.declare(R"code(
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iterator>

template <typename ELEMENT>
std::string toString(const std::vector<ELEMENT> & vec) {
  std::stringstream sstr;
  std::copy(begin(vec), end(vec), std::ostream_iterator<ELEMENT>(sstr, ","));
  return sstr.str();
}
)code");

  std::vector<double> vecComp{{ 12., 42., 0., -10. }};

  // Used to transport a value out of the interpreter, including lifetime
  // management.
  cling::Value val;

  // Invoke a function in the interpreter, passing a reference to an object
  // from compiled code.
  std::stringstream strCode;
  strCode << "toString(*(std::vector<double>*)" << (void*)&vecComp << ");";
  interp.evaluate(strCode.str(), val);

  // See whether everything is sane:
  std::cout << *((std::string*)val.getPtr()) << std::endl;
}

namespace clang {

  ///\brief Cleanup Parser state after a failed lookup.
  ///
  /// After a failed lookup we need to discard the remaining unparsed input,
  /// restore the original state of the incremental parsing flag, clear any
  /// pending diagnostics, restore the suppress diagnostics flag, and restore
  /// the spell checking language options.
  ///
  class ParserStateRAII {
    private:
    Parser* P;
    Preprocessor& PP;
    bool ResetIncrementalProcessing;
    bool OldSuppressAllDiagnostics;
    bool OldSpellChecking;
    DestroyTemplateIdAnnotationsRAIIObj CleanupTemplateIds;
    SourceLocation OldPrevTokLocation;
    unsigned short OldParenCount, OldBracketCount, OldBraceCount;
    unsigned OldTemplateParameterDepth;


    public:
    ParserStateRAII(Parser& p)
      : P(&p), PP(p.getPreprocessor()),
        ResetIncrementalProcessing(p.getPreprocessor()
                                     .isIncrementalProcessingEnabled()),
        OldSuppressAllDiagnostics(p.getPreprocessor().getDiagnostics()
                                    .getSuppressAllDiagnostics()),
        OldSpellChecking(p.getPreprocessor().getLangOpts().SpellChecking),
        CleanupTemplateIds(p), OldPrevTokLocation(p.PrevTokLocation),
        OldParenCount(p.ParenCount), OldBracketCount(p.BracketCount),
        OldBraceCount(p.BraceCount),
        OldTemplateParameterDepth(p.TemplateParameterDepth)
    {
    }

    ~ParserStateRAII()
    {
      //
      // Advance the parser to the end of the file, and pop the include stack.
      //
      // Note: Consuming the EOF token will pop the include stack.
      //
      P->SkipUntil(tok::eof);
      PP.enableIncrementalProcessing(ResetIncrementalProcessing);
      // Doesn't reset the diagnostic mappings
      P->getActions().getDiagnostics().Reset(/*soft=*/true);
      PP.getDiagnostics().setSuppressAllDiagnostics(OldSuppressAllDiagnostics);
      const_cast<LangOptions&>(PP.getLangOpts()).SpellChecking =
        OldSpellChecking;

      P->PrevTokLocation = OldPrevTokLocation;
      P->ParenCount = OldParenCount;
      P->BracketCount = OldBracketCount;
      P->BraceCount = OldBraceCount;
      P->TemplateParameterDepth = OldTemplateParameterDepth;
    }
  };
}

int main(int argc, char** argv) {

  cling::Interpreter interp(argc, argv, LLVMRESDIR);

  // Prerequisites to simplify the problem.
  // Unless we declare it outside the header file included, we have this error:
  // /cling/src/tools/cling/lib/Interpreter/../../../clang/include/clang/Sema/Sema.h:6832:
  // clang::Sema::SavePendingInstantiationsRAII::~SavePendingInstantiationsRAII(): Assertion
  // `S.PendingInstantiations.empty() && "there shouldn't be any pending instantiations"' failed.
  // Aborted (core dumped)
  interp.declare(R"code(#include "cling/Interpreter/Interpreter.h")code");

  clang::CompilerInstance* CI = interp.getCI();
  clang::Preprocessor & PP = CI->getPreprocessor();

  clang::FileManager & fileMgr = PP.getFileManager();
  const clang::FileEntry *Entry = fileMgr.getFile("test_header.h");


  const CodeCompleteOptions &Opts = CI->getFrontendOpts().CodeCompleteOpts;
  cling::Interpreter *interp_pointer = &interp;
  Completer * completer = new Completer(Opts, interp_pointer, CI);
  PP.SetCodeCompletionPoint(Entry, 2, 34);

  cling::Transaction *T = 0;

  CI->setCodeCompletionConsumer(completer);
  clang::Sema & sem = CI->getSema();
  sem.CodeCompleter = completer;

  {
    clang::ParserStateRAII parserRAII(interp.getParser());

    //  Tell the diagnostic engine to ignore all diagnostics.
    //
    //bool OldSuppressAllDiagnostics(PP.getDiagnostics()
    //                                 .getSuppressAllDiagnostics());
    //PP.getDiagnostics().setSuppressAllDiagnostics(true);

    interp.declare(R"code(#include "test_header.h")code", &T);

    //PP.getDiagnostics().setSuppressAllDiagnostics(OldSuppressAllDiagnostics);
  }
  CI->setCodeCompletionConsumer(nullptr);
  PP.clearCodeCompletionHandler();
  sem.CodeCompleter = nullptr;
  interp.declare(R"code(#include "test_header.h")code");

  return 0;
}