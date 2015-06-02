//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "ClingPragmas.h"

#include "cling/Interpreter/Interpreter.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "clang/Parse/Parser.h"

using namespace cling;
using namespace clang;

namespace {
   static void replaceEnvVars(std::string &Path){

    std::size_t fpos = Path.find("$");

    while (fpos != std::string::npos) {
      std::size_t spos = Path.find("/", fpos + 1);
      std::size_t length;

      if (spos != std::string::npos) // if we found a "/"
        length = spos - fpos;
      else // we didn't find any "/"
        length = Path.length();

      std::string envVar = Path.substr(fpos + 1, length -1); //"HOME"
      std::string fullPath{getenv(envVar.c_str())};
      Path.replace(fpos, length, fullPath);
      fpos = Path.find("$", fpos + 1); //search for next env variable
    }
  }
  static std::string HandlePragmaHelper(Preprocessor &PP,
                      PragmaIntroducerKind Introducer,
                      Token &FirstToken,
                      Interpreter &m_Interp,
                      std::string errorMessage,
                      std::string pragmaInst){
   struct SkipToEOD_t {
        Preprocessor& m_PP;
        SkipToEOD_t(Preprocessor& PP): m_PP(PP) {}
        ~SkipToEOD_t() { m_PP.DiscardUntilEndOfDirective(); }
      } SkipToEOD(PP);

      Token Tok;
      PP.Lex(Tok);
      if (Tok.isNot(tok::l_paren)) {
        llvm::errs() << errorMessage;
        return "";
      }
      std::string Literal;
      if (!PP.LexStringLiteral(Tok, Literal, pragmaInst.c_str(),
                               false /*allowMacroExpansion*/)) {
        // already diagnosed.
        return "";
      }
      if((pragmaInst == "pragma cling add_include_path") || (pragmaInst == "pragma cling add_library_path")){
        { if (Literal.find("$") != std::string::npos)
           replaceEnvVars(Literal);
        }
      }

      clang::Parser& P = m_Interp.getParser();
      Parser::ParserCurTokRestoreRAII savedCurToken(P);
      // After we have saved the token reset the current one to something which
      // is safe (semi colon usually means empty decl)
      Token& CurTok = const_cast<Token&>(P.getCurToken());
      CurTok.setKind(tok::semi);

      return Literal;
  }

  class PHLoad: public PragmaHandler {
    Interpreter& m_Interp;

  public:
    PHLoad(Interpreter& interp):
      PragmaHandler("load"), m_Interp(interp) {}

    void HandlePragma(Preprocessor &PP,
                      PragmaIntroducerKind Introducer,
                      Token &FirstToken) override {
      // TODO: use Diagnostics!
      std::string file;
      file = HandlePragmaHelper(PP, Introducer, FirstToken, m_Interp,
                                           "cling::PHLoad: expect '(' after #pragma cling load!\n", "pragma cling load");
      Preprocessor::CleanupAndRestoreCacheRAII cleanupRAII(PP);
      // We can't PushDeclContext, because we go up and the routine that pops
      // the DeclContext assumes that we drill down always.
      // We have to be on the global context. At that point we are in a
      // wrapper function so the parent context must be the global.
      TranslationUnitDecl* TU
        = m_Interp.getCI()->getASTContext().getTranslationUnitDecl();
      Sema::ContextAndScopeRAII pushedDCAndS(m_Interp.getSema(),
                                             TU, m_Interp.getSema().TUScope);
      Interpreter::PushTransactionRAII pushedT(&m_Interp);
      m_Interp.loadFile(file,true /*allowSharedLib*/);
    }
  };

  class PHAddIncPath: public PragmaHandler {
    Interpreter& m_Interp;

  public:
    PHAddIncPath(Interpreter& interp):
      PragmaHandler("add_include_path"), m_Interp(interp) {}

    void HandlePragma(Preprocessor &PP,
                      PragmaIntroducerKind Introducer,
                      Token &FirstToken) override {
      // TODO: use Diagnostics!
      m_Interp.AddIncludePath(HandlePragmaHelper(PP, Introducer, FirstToken, m_Interp,
                                                 "cling::PHAddIncPath: expect '(' after #pragma cling add_include_path!\n",
                                                 "pragma cling add_include_path"));
    }
  };

  class PHAddLibraryPath: public PragmaHandler {
    Interpreter& m_Interp;

  public:
    PHAddLibraryPath(Interpreter& interp):
      PragmaHandler("add_library_path"), m_Interp(interp) {}

    void HandlePragma(Preprocessor &PP,
                      PragmaIntroducerKind Introducer,
                      Token &FirstToken) override {
      // TODO: use Diagnostics!
      InvocationOptions& Opts = m_Interp.getOptions();
      Opts.LibSearchPath.push_back( HandlePragmaHelper(PP, Introducer, FirstToken, m_Interp,
                                                       "cling::PHAddLibraryPath: expect '(' after #pragma cling add_library_path!\n",
                                                       "pragma cling add_library_path"));
    }
  };
}

void cling::addClingPragmas(Interpreter& interp) {
  Preprocessor& PP = interp.getCI()->getPreprocessor();
  // PragmaNamespace / PP takes ownership of sub-handlers.
  PP.AddPragmaHandler("cling", new PHLoad(interp));
  PP.AddPragmaHandler("cling", new PHAddIncPath(interp));
  PP.AddPragmaHandler("cling", new PHAddLibraryPath(interp));
}
