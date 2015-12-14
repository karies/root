#include "cling/Utils/ASTImportSource.h"

using namespace clang;

namespace cling {
  namespace utils {
    ASTImportSource::ASTImportSource(cling::Interpreter *interpreter_first,
                                     cling::Interpreter *interpreter_second) :
      m_first_Interp(interpreter_first), m_second_Interp(interpreter_second) {

      m_translationUnitI1 = m_first_Interp->getCI()->getASTContext().getTranslationUnitDecl();
      m_translationUnitI2 = m_second_Interp->getCI()->getASTContext().getTranslationUnitDecl();

    }

    void ASTImportSource::InitializeSema(Sema &S) {
      m_Sema = &S;
    }

    void ASTImportSource::ForgetSema() {
      m_Sema = nullptr;
    }

    void ASTImportSource::ImportDecl(Decl *declFrom,
                                     ASTImporter &importer,
                                     DeclarationName &Name,
                                     DeclarationName &declNameI1,
                                     const DeclContext *DC) {

      Decl *importedDecl = nullptr;
      /* Don't do the import if we have a Function Template.
       * Not supported by clang. */
      if (!(declFrom->isFunctionOrFunctionTemplate() && declFrom->isTemplateDecl()))
        importedDecl = importer.Import(declFrom);

      if (importedDecl) {
        /* Import this Decl in the map we own. Not used for now
         * m_Decls_map[Name.getAsString()] = std::make_pair(importedDecl, declFrom); */
        std::vector < NamedDecl * > declVector;
        declVector.push_back((NamedDecl *) importedDecl);
        llvm::ArrayRef < NamedDecl * > FoundDecls(declVector);
        SetExternalVisibleDeclsForName(DC, ((NamedDecl *) importedDecl)->getDeclName(),
                                       FoundDecls);

        /* And also put the Decl I found from the first Interpreter
         * in the map of the second Interpreter to have it for the future. */
        m_DeclName_map[Name.getAsString()] = declNameI1;
      }
    }

    void ASTImportSource::ImportDeclContext(DeclContext *declContextFrom,
                                            ASTImporter &importer,
                                            DeclarationName &Name,
                                            DeclarationName &declNameI1,
                                            const DeclContext *DC) {

      DeclContext *importedDeclContext =
        importer.ImportContext(declContextFrom);

      if (importedDeclContext) {
        /* And also put the declaration context I found from the first Interpreter
         * in the map of the second Interpreter to have it for the future. */
        m_DeclContexts_map[importedDeclContext] = declContextFrom;

        /* Not used for now.
         * m_DeclContextsNames_map[Name.getAsString()] =
         * std::make_pair(importedDeclContext,declContextFrom); */
        importedDeclContext->setHasExternalVisibleStorage(true);

        std::vector < NamedDecl * > declVector;
        declVector.push_back((NamedDecl *) importedDeclContext);
        llvm::ArrayRef < NamedDecl * > FoundDecls(declVector);
        SetExternalVisibleDeclsForName(DC, ((NamedDecl *) importedDeclContext)->getDeclName(),
                                       FoundDecls);

        /* And also put the Decl I found from the first Interpreter
         * in the map of the second Interpreter to have it for the future. */
        m_DeclName_map[Name.getAsString()] = declNameI1;
      }
    }

    bool ASTImportSource::Import(DeclContext::lookup_result lookup_result,
                                 ASTContext &from_ASTContext,
                                 ASTContext &to_ASTContext,
                                 const DeclContext *DC,
                                 DeclarationName &Name,
                                 DeclarationName &declNameI1) {

      /* Check if we found this Name in the first interpreter */
      if (lookup_result.empty())
        return false;

      /* Prepare to import the Decl(Context)  we found in the
       * second interpreter */
      FileManager& fm =  m_second_Interp->getCI()->getFileManager();

      /*****************************ASTImporter**********************************/
      ASTImporter importer(to_ASTContext, fm, from_ASTContext, fm,
        /*MinimalImport : ON*/ true);
      /**************************************************************************/

      /* If this Name we are looking for is for example a Namespace,
       * then it is a Decl Context. */
      if ((*lookup_result.data())->getKind() == Decl::Namespace) {
        DeclContext *declContextFrom =
          llvm::cast<DeclContext>(*lookup_result.data());
        ImportDeclContext(declContextFrom, importer, Name, declNameI1, DC);
      } else { /* If this name is just a Decl. */
        /* Check if we have more than one results, this means that it
         * may be an overloaded function. */
        if (lookup_result.size() > 1) {
          DeclContext::lookup_iterator I;
          DeclContext::lookup_iterator E;
          for (I = lookup_result.begin(), E = lookup_result.end(); I != E; ++I) {
            NamedDecl *D = *I;
            Decl *declFrom = llvm::cast<Decl>(D);
            ImportDecl(declFrom, importer, Name, declNameI1, DC);
          }
        } else {
          Decl *declFrom = llvm::cast<Decl>(*lookup_result.data());
          ImportDecl(declFrom, importer, Name, declNameI1, DC);
        }
      }
      return true;
    }

    bool ASTImportSource::FindExternalVisibleDeclsByName(const DeclContext *DC,
                                                         DeclarationName Name) {
      assert(DC->hasExternalVisibleStorage() &&
             "DeclContext has no visible decls in storage");

      /* clang will call FindExternalVisibleDeclsByName with an
       * IdentifierInfo valid for the second interpreter. Get the
       * IdentifierInfo's StringRef representation.
       * Get the identifier info from the first interpreter
       * for this Name. */
      llvm::StringRef name(Name.getAsString());
      IdentifierTable &identsI1 =
        m_first_Interp->getCI()->getASTContext().Idents;
      IdentifierInfo &IdentifierInfoI1 = identsI1.get(name);
      DeclarationName declNameI1(&IdentifierInfoI1);

      /* Check if we have already imported this Decl (Context). */
      if (m_DeclName_map.find(Name.getAsString()) != m_DeclName_map.end())
        return true;

      DeclContext::lookup_result lookup_result;
      /* If we are not looking for this Name in the Translation Unit
       * but instead inside a namespace, */
      if (!DC->isTranslationUnit()) {
        /* Search in the map of the stored Decl Contexts for this
         * namespace. */
        if (m_DeclContexts_map.find(DC) != m_DeclContexts_map.end()) {
          /* If DC was found before and is already in the map,
           * then do the lookup using the stored pointer. */
          DeclContext *originalDeclContext = m_DeclContexts_map.find(DC)->second;

          Decl *fromDeclContext = Decl::castFromDeclContext(originalDeclContext);
          ASTContext &from_ASTContext = fromDeclContext->getASTContext();

          Decl *toDeclContext = Decl::castFromDeclContext(DC);
          ASTContext &to_ASTContext = toDeclContext->getASTContext();

          lookup_result = originalDeclContext->lookup(declNameI1);
          /* Do the import */
          if (Import(lookup_result, from_ASTContext, to_ASTContext, DC, Name, declNameI1))
            return true;
        }
      } else { /* Otherwise search in the Translation Unit. */
        ASTContext &from_ASTContext = m_translationUnitI1->getASTContext();
        ASTContext &to_ASTContext = m_translationUnitI2->getASTContext();
        DeclContext *TUDeclContextI1 = TranslationUnitDecl::castToDeclContext(m_translationUnitI1);

        lookup_result = TUDeclContextI1->lookup(declNameI1);
        /* Do the import */
        if (Import(lookup_result, from_ASTContext, to_ASTContext, DC, Name, declNameI1))
          return true;
      }
      return false;
    }
  } // end namespace utils
} // end namespace cling
