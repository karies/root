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
  class PHLoad: public PragmaHandler {
    Interpreter& m_Interp;

  public:
    PHLoad(Interpreter& interp):
      PragmaHandler("load"), m_Interp(interp) {}

    void HandlePragma(Preprocessor &PP,
                      PragmaIntroducerKind Introducer,
                      Token &FirstToken) override {
      // TODO: use Diagnostics!

      struct SkipToEOD_t {
        Preprocessor& m_PP;
        SkipToEOD_t(Preprocessor& PP): m_PP(PP) {}
        ~SkipToEOD_t() { m_PP.DiscardUntilEndOfDirective(); }
      } SkipToEOD(PP);

      Token Tok;
      PP.Lex(Tok);
      if (Tok.isNot(tok::l_paren)) {
        llvm::errs() << "cling::PHLoad: expect '(' after #pragma cling load!\n";
        return;
      }
      std::string FileName;
      if (!PP.LexStringLiteral(Tok, FileName, "pragma cling load",
                               false /*allowMacroExpansion*/)) {
        // already diagnosed.
        return;
      }
      Preprocessor::CleanupAndRestoreCacheRAII cleanupRAII(PP);
      clang::Parser& P = m_Interp.getParser();
      Parser::ParserCurTokRestoreRAII savedCurToken(P);
      // After we have saved the token reset the current one to something which
      // is safe (semi colon usually means empty decl)
      Token& CurTok = const_cast<Token&>(P.getCurToken());
      CurTok.setKind(tok::semi);

      // We can't PushDeclContext, because we go up and the routine that pops
      // the DeclContext assumes that we drill down always.
      // We have to be on the global context. At that point we are in a
      // wrapper function so the parent context must be the global.
      TranslationUnitDecl* TU
        = m_Interp.getCI()->getASTContext().getTranslationUnitDecl();
      Sema::ContextAndScopeRAII pushedDCAndS(m_Interp.getSema(),
                                             TU, m_Interp.getSema().TUScope);

      Interpreter::PushTransactionRAII pushedT(&m_Interp);
      m_Interp.loadFile(FileName, true /*allowSharedLib*/);
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

      struct SkipToEOD_t {
        Preprocessor& m_PP;
        SkipToEOD_t(Preprocessor& PP): m_PP(PP) {}
        ~SkipToEOD_t() { m_PP.DiscardUntilEndOfDirective(); }
      } SkipToEOD(PP);

      Token Tok;
      PP.Lex(Tok);
      if (Tok.isNot(tok::l_paren)) {
        llvm::errs() << "cling::PHAddIncPath: expect '(' after #pragma cling add_include_path!\n";
        return;
      }
      std::string IncPath;
      if (!PP.LexStringLiteral(Tok, IncPath, "pragma cling add_include_path",
                               false /*allowMacroExpansion*/)) {
        // already diagnosed.
        return;
      }
      if (IncPath.find("$") != std::string::npos)
        m_Interp.replaceEnvVars(IncPath);

      Preprocessor::CleanupAndRestoreCacheRAII cleanupRAII(PP);
      clang::Parser& P = m_Interp.getParser();
      Parser::ParserCurTokRestoreRAII savedCurToken(P);
      // After we have saved the token reset the current one to something which
      // is safe (semi colon usually means empty decl)
      Token& CurTok = const_cast<Token&>(P.getCurToken());
      CurTok.setKind(tok::semi);

      // We can't PushDeclContext, because we go up and the routine that pops
      // the DeclContext assumes that we drill down always.
      // We have to be on the global context. At that point we are in a
      // wrapper function so the parent context must be the global.
      TranslationUnitDecl* TU
        = m_Interp.getCI()->getASTContext().getTranslationUnitDecl();
      Sema::ContextAndScopeRAII pushedDCAndS(m_Interp.getSema(),
                                             TU, m_Interp.getSema().TUScope);

      Interpreter::PushTransactionRAII pushedT(&m_Interp);
      m_Interp.AddIncludePath(IncPath);
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

      struct SkipToEOD_t {
        Preprocessor& m_PP;
        SkipToEOD_t(Preprocessor& PP): m_PP(PP) {}
        ~SkipToEOD_t() { m_PP.DiscardUntilEndOfDirective(); }
      } SkipToEOD(PP);

      Token Tok;
      PP.Lex(Tok);
      if (Tok.isNot(tok::l_paren)) {
        llvm::errs() << "cling::PHAddLibraryPath: expect '(' after #pragma cling add_library_path!\n";
        return;
      }
      std::string LibPath;
      if (!PP.LexStringLiteral(Tok, LibPath, "pragma cling add_library_path",
                               false /*allowMacroExpansion*/)) {
        // already diagnosed.
        return;
      }
      //if the path contains environmental variable
      if (LibPath.find("$") != std::string::npos)
        m_Interp.replaceEnvVars(LibPath);

      Preprocessor::CleanupAndRestoreCacheRAII cleanupRAII(PP);
      clang::Parser& P = m_Interp.getParser();
      Parser::ParserCurTokRestoreRAII savedCurToken(P);
      // After we have saved the token reset the current one to something which
      // is safe (semi colon usually means empty decl)
      Token& CurTok = const_cast<Token&>(P.getCurToken());
      CurTok.setKind(tok::semi);

      // We can't PushDeclContext, because we go up and the routine that pops
      // the DeclContext assumes that we drill down always.
      // We have to be on the global context. At that point we are in a
      // wrapper function so the parent context must be the global.
      TranslationUnitDecl* TU
        = m_Interp.getCI()->getASTContext().getTranslationUnitDecl();
      Sema::ContextAndScopeRAII pushedDCAndS(m_Interp.getSema(),
                                             TU, m_Interp.getSema().TUScope);

      Interpreter::PushTransactionRAII pushedT(&m_Interp);
      InvocationOptions& Opts = m_Interp.getOptions();
      Opts.LibSearchPath.push_back(LibPath);
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
