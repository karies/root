//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Elisavet Sakellari <elisavet.sakellari@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------
#ifndef CLING_ASTIMPORTSOURCE_H
#define CLING_ASTIMPORTSOURCE_H

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"

#include <string>
#include <map>

namespace clang {
  class ASTContext;
  class Decl;
  class DeclContext;
  class DeclarationName;
  class NamedDecl;
  class Sema;
}

namespace cling {
  class Interpreter;
}

namespace cling {

  ///\brief This class implements the importing functionality between two
  /// interpreters.
  ///
  /// It creates a clang::ASTImporter object which does the import of the Decls
  /// and DeclContexts from the parent to the child interpreter.
  /// This class is created when setting up the child Interpreter and
  /// “passed” to its Translation Unit as an external AST source.
  ///
  class ASTImportSource : public clang::ExternalASTSource {
    private:
      cling::Interpreter *m_parent_Interp;
      cling::Interpreter *m_child_Interp;

      clang::Sema *m_Sema;

      ///\brief We keep a mapping between the imported DeclContexts
      /// and the original ones from of the first Interpreter.
      /// Key: imported(child) DeclContext
      /// Value: parent DeclContext
      ///
      std::map<const clang::DeclContext *, clang::DeclContext *>
                                                         m_childToParentDC;

      ///\brief A map for all the imported Decls (Contexts)
      /// according to their names.
      /// Key    Name of the Decl(Context) as a string in the child.
      /// Value: The DeclarationName of this Decl(Context) comes
      ///        from the parent Interpreter.
      ///
      std::map<clang::DeclarationName, clang::DeclarationName >
                                                       m_childToParentName;

    public:
      ASTImportSource(cling::Interpreter *parent_interpreter,
                      cling::Interpreter *child_interpreter);

      ~ASTImportSource() { };

      bool FindExternalVisibleDeclsByName(const clang::DeclContext *childCurrentDC,
                                    clang::DeclarationName childDeclName) override;

      void InitializeSema(clang::Sema &S) { m_Sema = &S; }

      void ForgetSema() { m_Sema = nullptr; }

      bool Import(clang::DeclContext::lookup_result lookup_result,
                  clang::ASTContext &from_ASTContext,
                  clang::ASTContext &to_ASTContext,
                  const clang::DeclContext *childCurrentDC,
                  clang::DeclarationName &childDeclName,
                  clang::DeclarationName &parentDeclName);

      void ImportDeclContext(clang::DeclContext *parentDC,
                             clang::ASTImporter &importer,
                             clang::DeclarationName &childDeclName,
                             clang::DeclarationName &parentDeclName,
                             const clang::DeclContext *childCurrentDC);

      void ImportDecl(clang::Decl *declToImport,
                      clang::ASTImporter &importer,
                      clang::DeclarationName &childDeclName,
                      clang::DeclarationName &parentDeclName,
                      const clang::DeclContext *childCurrentDC);
  };
} // end namespace cling
#endif //CLING_ASTIMPORTSOURCE_H
