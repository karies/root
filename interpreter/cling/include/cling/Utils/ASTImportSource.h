#ifndef CLING_ASTIMPORTSOURCE_H
#define CLING_ASTIMPORTSOURCE_H

#include "cling/Interpreter/Interpreter.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"

#include <string>
#include <map>

namespace clang {
  class ASTContext;
  class Expr;
  class Decl;
  class DeclContext;
  class DeclarationName;
  class GlobalDecl;
  class FunctionDecl;
  class IntegerLiteral;
  class NamedDecl;
  class NamespaceDecl;
  class NestedNameSpecifier;
  class QualType;
  class Sema;
  class TagDecl;
  class TemplateDecl;
  class Type;
  class TypedefNameDecl;
}

namespace cling {
  namespace utils {

    class ASTImportSource : public clang::ExternalASTSource {

      private:
        cling::Interpreter *m_first_Interp;
        cling::Interpreter *m_second_Interp;
        const clang::TranslationUnitDecl *m_translationUnitI1;
        const clang::TranslationUnitDecl *m_translationUnitI2;

        clang::Sema *m_Sema;
        /* A map for the DeclContext-to-DeclContext correspondence
         * with DeclContexts pointers. */
        std::map<const clang::DeclContext *, clang::DeclContext *> m_DeclContexts_map;
        /* A map for all the imported Decls (Contexts). */
        std::map <std::string, clang::DeclarationName> m_DeclName_map;

      public:
        ASTImportSource(cling::Interpreter *interpreter_first,
                        cling::Interpreter *interpreter_second);

        ~ASTImportSource() { };

        bool
          FindExternalVisibleDeclsByName(const clang::DeclContext *DC, clang::DeclarationName Name);

        void InitializeSema(clang::Sema &S);

        void ForgetSema();

        bool Import(clang::DeclContext::lookup_result lookup_result,
                    clang::ASTContext &from_ASTContext,
                    clang::ASTContext &to_ASTContext,
                    const clang::DeclContext *DC,
                    clang::DeclarationName &Name,
                    clang::DeclarationName &declNameI1);

        void ImportDeclContext(clang::DeclContext *declContextFrom,
                               clang::ASTImporter &importer,
                               clang::DeclarationName &Name,
                               clang::DeclarationName &declNameI1,
                               const clang::DeclContext *DC);

        void ImportDecl(clang::Decl *declFrom,
                        clang::ASTImporter &importer,
                        clang::DeclarationName &Name,
                        clang::DeclarationName &declNameI1,
                        const clang::DeclContext *DC);

        cling::Interpreter *getInterpreter() { return m_first_Interp; }
    };
  } // end namespace utils
} // end namespace cling
#endif //CLING_ASTIMPORTSOURCE_H
