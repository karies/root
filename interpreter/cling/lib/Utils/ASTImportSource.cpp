#include "cling/Utils/ASTImportSource.h"

using namespace clang;

namespace cling {
  namespace utils {
    ASTImportSource::ASTImportSource(cling::Interpreter *parent_interpreter,
                                     cling::Interpreter *child_interpreter) :
      m_parent_Interp(parent_interpreter), m_child_Interp(child_interpreter) {
    }

    void ASTImportSource::ImportDecl(Decl *declToImport,
                                     ASTImporter &importer,
                                     DeclarationName &childDeclName,
                                     DeclarationName &parentDeclName,
                                     const DeclContext *childCurrentDeclContext) {

      Decl *importedDecl = nullptr;
      /* Don't do the import if we have a Function Template.
       * Not supported by clang. */
      if (!(declToImport->isFunctionOrFunctionTemplate() && declToImport->isTemplateDecl()))
        importedDecl = importer.Import(declToImport);

      if (importedDecl) {
        /* Import this Decl in the map we own. Not used for now
         * m_Decls_map[Name.getAsString()] = std::make_pair(importedDecl, declToImport); */
        std::vector < NamedDecl * > declVector;
        declVector.push_back((NamedDecl *) importedDecl);
        llvm::ArrayRef < NamedDecl * > FoundDecls(declVector);
        SetExternalVisibleDeclsForName(childCurrentDeclContext,
                                       ((NamedDecl *) importedDecl)->getDeclName(),
                                       FoundDecls);

        /* And also put the Decl I found from the parent Interpreter
         * in the map of the child Interpreter to have it for the future. */
        m_DeclName_map[childDeclName.getAsString()] = parentDeclName;
      }
    }

    void ASTImportSource::ImportDeclContext(DeclContext *declContextToImport,
                                            ASTImporter &importer,
                                            DeclarationName &childDeclName,
                                            DeclarationName &parentDeclName,
                                            const DeclContext *childCurrentDeclContext) {

      DeclContext *importedDeclContext =
        importer.ImportContext(declContextToImport);

      if (importedDeclContext) {
        /* And also put the declaration context I found from the parent Interpreter
         * in the map of the child Interpreter to have it for the future. */
        m_DeclContexts_map[importedDeclContext] = declContextToImport;

        /* Not used for now.
         * m_DeclContextsNames_map[childDeclName.getAsString()] =
         * std::make_pair(importedDeclContext,declContextToImport); */
        importedDeclContext->setHasExternalVisibleStorage(true);

        std::vector < NamedDecl * > declVector;
        declVector.push_back((NamedDecl *) importedDeclContext);
        llvm::ArrayRef < NamedDecl * > FoundDecls(declVector);
        SetExternalVisibleDeclsForName(childCurrentDeclContext,
                                       ((NamedDecl *) importedDeclContext)->getDeclName(),
                                       FoundDecls);

        /* And also put the Decl I found from the parent Interpreter
         * in the map of the child Interpreter to have it for the future. */
        m_DeclName_map[childDeclName.getAsString()] = parentDeclName;
      }
    }

    bool ASTImportSource::Import(DeclContext::lookup_result lookup_result,
                                 ASTContext &from_ASTContext,
                                 ASTContext &to_ASTContext,
                                 const DeclContext *childCurrentDeclContext,
                                 DeclarationName &childDeclName,
                                 DeclarationName &parentDeclName) {

      /* Check if we found this Name in the parent interpreter */
      if (lookup_result.empty())
        return false;

      /* Prepare to import the Decl(Context)  we found in the
       * child interpreter */
      const FileSystemOptions systemOptions;
      FileManager fm(systemOptions, nullptr);

      /*****************************ASTImporter**********************************/
      ASTImporter importer(to_ASTContext, fm, from_ASTContext, fm,
        /*MinimalImport : ON*/ true);
      /**************************************************************************/

      /* If this Name we are looking for is for example a Namespace,
       * then it is a Decl Context. */
      if ((*lookup_result.data())->getKind() == Decl::Namespace) {
        DeclContext *declContextToImport =
          llvm::cast<DeclContext>(*lookup_result.data());
        ImportDeclContext(declContextToImport, importer, childDeclName,
                          parentDeclName, childCurrentDeclContext);
      } else { /* If this name is just a Decl. */
        /* Check if we have more than one results, this means that it
         * may be an overloaded function. */
        if (lookup_result.size() > 1) {
          DeclContext::lookup_iterator I;
          DeclContext::lookup_iterator E;
          for (I = lookup_result.begin(), E = lookup_result.end(); I != E; ++I) {
            NamedDecl *D = *I;
            Decl *declToImport = llvm::cast<Decl>(D);
            ImportDecl(declToImport, importer, childDeclName,
                       parentDeclName, childCurrentDeclContext);
          }
        } else {
          Decl *declToImport = llvm::cast<Decl>(*lookup_result.data());
          ImportDecl(declToImport, importer, childDeclName,
                     parentDeclName, childCurrentDeclContext);
        }
      }
      return true;
    }

    ///\brief This is the most important function of the class ASTImportSource
    /// since from here initiates the lookup and import part of the missing
    /// Decl(s) (Contexts).
    ///
    bool ASTImportSource::FindExternalVisibleDeclsByName(
      const DeclContext *childCurrentDeclContext, DeclarationName childDeclName) {

      assert(childCurrentDeclContext->hasExternalVisibleStorage() &&
             "DeclContext has no visible decls in storage");

      /* clang will call FindExternalVisibleDeclsByName with an
       * IdentifierInfo valid for the child interpreter. Get the
       * IdentifierInfo's StringRef representation.
       * Get the identifier info from the parent interpreter
       * for this Name. */
      llvm::StringRef name(childDeclName.getAsString());
      IdentifierTable &parentIdentifierTable =
        m_parent_Interp->getCI()->getASTContext().Idents;
      IdentifierInfo &parentIdentifierInfo = parentIdentifierTable.get(name);
      DeclarationName parentDeclName(&parentIdentifierInfo);

      /* Check if we have already imported this Decl (Context). */
      if (m_DeclName_map.find(childDeclName.getAsString()) != m_DeclName_map.end())
        return true;

      DeclContext::lookup_result lookup_result;
      /* If we are not looking for this Name in the Translation Unit
       * but instead inside a namespace, */
      if (!childCurrentDeclContext->isTranslationUnit()) {
        /* Search in the map of the stored Decl Contexts for this
         * namespace. */
        if (m_DeclContexts_map.find(childCurrentDeclContext) != m_DeclContexts_map.end()) {
          /* If childCurrentDeclContext was found before and is already in the map,
           * then do the lookup using the stored pointer. */
          DeclContext *originalDeclContext =
            m_DeclContexts_map.find(childCurrentDeclContext)->second;

          Decl *fromDeclContext = Decl::castFromDeclContext(originalDeclContext);
          ASTContext &from_ASTContext = fromDeclContext->getASTContext();

          Decl *toDeclContext = Decl::castFromDeclContext(childCurrentDeclContext);
          ASTContext &to_ASTContext = toDeclContext->getASTContext();

          lookup_result = originalDeclContext->lookup(parentDeclName);
          /* Do the import */
          if (Import(lookup_result, from_ASTContext, to_ASTContext,
                     childCurrentDeclContext, childDeclName, parentDeclName))
            return true;
        }
      } else { /* Otherwise search in the Translation Unit. */
        ASTContext &from_ASTContext = m_parent_Interp->getCI()->getASTContext();
        ASTContext &to_ASTContext = m_child_Interp->getCI()->getASTContext();

        /* Do the lookup in the Translation Unit of parent Interpreter */
        DeclContext *parentTUDeclContext =
          TranslationUnitDecl::castToDeclContext(from_ASTContext.getTranslationUnitDecl());

        //to_ASTContext.getTranslationUnitDecl()

        lookup_result = parentTUDeclContext->lookup(parentDeclName);
        /* Do the import */
        if (Import(lookup_result, from_ASTContext, to_ASTContext,
                   childCurrentDeclContext, childDeclName, parentDeclName))
          return true;
      }
      return false;
    }
  } // end namespace utils
} // end namespace cling
