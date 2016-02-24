#include "ASTImportSource.h"
#include "cling/Interpreter/Interpreter.h"

using namespace clang;

namespace cling {

    ASTImportSource::ASTImportSource(cling::Interpreter *parent_interpreter,
    cling::Interpreter *child_interpreter) :
    m_parent_Interp(parent_interpreter), m_child_Interp(child_interpreter) {

      clang::DeclContext *parentTUDeclContext =
        clang::TranslationUnitDecl::castToDeclContext(
          m_parent_Interp->getCI()->getASTContext().getTranslationUnitDecl());

      clang::DeclContext *childTUDeclContext =
        clang::TranslationUnitDecl::castToDeclContext(
          m_child_Interp->getCI()->getASTContext().getTranslationUnitDecl());

      // Also keep in the map of Decl Contexts the Translation Unit Decl Context
      m_childToParentDC_map[childTUDeclContext] = parentTUDeclContext;
    }

    void ASTImportSource::ImportDecl(Decl *parentDecl,
                                     ASTImporter &importer,
                                     DeclarationName &childDeclName,
                                     DeclarationName &parentDeclName,
                                     const DeclContext *childCurrentDC) {

      // Don't do the import if we have a Function Template.
      // Not supported by clang.
      // FIXME: This is also a temporary check. Will be de-activated
      // once clang supports the import of function templates.
      if (parentDecl->isFunctionOrFunctionTemplate() && parentDecl->isTemplateDecl())
        return;

      if (Decl *childDecl = importer.Import(parentDecl)) {
        if (NamedDecl *childNamedDecl = llvm::dyn_cast<NamedDecl>(childDecl)) {
          std::vector < NamedDecl * > declVector{childNamedDecl};
          llvm::ArrayRef < NamedDecl * > FoundDecls(declVector);
          SetExternalVisibleDeclsForName(childCurrentDC,
                                         childNamedDecl->getDeclName(),
                                         FoundDecls);
        }
        // Put the name of the Decl imported with the
        // DeclarationName coming from the parent, in  my map.
        m_childToParentName_map[childDeclName] = parentDeclName;
      }
    }

    void ASTImportSource::ImportDeclContext(DeclContext *parentDC,
                                            ASTImporter &importer,
                                            DeclarationName &childDeclName,
                                            DeclarationName &parentDeclName,
                                            const DeclContext *childCurrentDC) {

      // If this declContext is a namespace, import only its original declaration.
      if (NamespaceDecl *namespaceDecl
            = llvm::dyn_cast<NamespaceDecl>(parentDC)) {
        parentDC = namespaceDecl->getOriginalNamespace();
      }

      if (DeclContext *childDC = importer.ImportContext(parentDC)) {

        childDC->setHasExternalVisibleStorage(true);

        if (NamedDecl *childNamedDecl = llvm::dyn_cast<NamedDecl>(childDC)) {
          std::vector < NamedDecl * > declVector{childNamedDecl};
          llvm::ArrayRef < NamedDecl * > FoundDecls(declVector);
          SetExternalVisibleDeclsForName(childCurrentDC,
                                         childNamedDecl->getDeclName(),
                                         FoundDecls);
        }
        // Put the name of the DeclContext imported with the
        // DeclarationName coming from the parent, in  my map.
        m_childToParentName_map[childDeclName] = parentDeclName;

        // And also put the declaration context I found from the parent Interpreter
        // in the map of the child Interpreter to have it for the future.
        m_childToParentDC_map[childDC] = parentDC;
      }
    }

    bool ASTImportSource::Import(DeclContext::lookup_result lookup_result,
                                 ASTContext &from_ASTContext,
                                 ASTContext &to_ASTContext,
                                 const DeclContext *childCurrentDC,
                                 DeclarationName &childDeclName,
                                 DeclarationName &parentDeclName) {
      // Prepare to import the Decl(Context)  we found in the
      // child interpreter by getting the file managers from
      // each interpreter.
      FileManager &child_FM = m_child_Interp->getCI()->getFileManager();
      FileManager &parent_FM = m_parent_Interp->getCI()->getFileManager();

      // Clang's ASTImporter
      ASTImporter importer(to_ASTContext, child_FM,
                           from_ASTContext, parent_FM,
                           /*MinimalImport : ON*/ true);

      for (DeclContext::lookup_iterator I = lookup_result.begin(),
             E = lookup_result.end();
             I != E; ++I) {
        // Check if this Name we are looking for is
        // a DeclContext (for example a Namespace, function etc.).
        if (DeclContext *parentDC = llvm::dyn_cast<DeclContext>(*I)) {

          ImportDeclContext(parentDC, importer, childDeclName,
                            parentDeclName, childCurrentDC);

        } else if (Decl *parentDecl = llvm::dyn_cast<Decl>(*I)) {

          // else it is a Decl
          ImportDecl(parentDecl, importer, childDeclName,
                     parentDeclName, childCurrentDC);
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

      //Check if we have already found this declaration Name before
      DeclarationName parentDeclName;
      std::map<clang::DeclarationName,
        clang::DeclarationName>::iterator II =  m_childToParentName_map.find(childDeclName);
      if (II !=  m_childToParentName_map.end()) {
        parentDeclName = II->second;
      } else {
        // Get the identifier info from the parent interpreter
        // for this Name.
        llvm::StringRef name(childDeclName.getAsString());
        IdentifierTable &parentIdentifierTable =
          m_parent_Interp->getCI()->getASTContext().Idents;
        IdentifierInfo &parentIdentifierInfo = parentIdentifierTable.get(name);
        DeclarationName parentDeclNameTemp(&parentIdentifierInfo);
        parentDeclName = parentDeclNameTemp;
      }

      // Search in the map of the stored Decl Contexts for this
      // Decl Context.
      std::map<const clang::DeclContext *, clang::DeclContext *>::iterator I;
      if ((I = m_childToParentDC_map.find(childCurrentDeclContext))
           != m_childToParentDC_map.end()) {
        // If childCurrentDeclContext was found before and is already in the map,
        // then do the lookup using the stored pointer.
        DeclContext::lookup_result lookup_result =
          I->second->lookup(parentDeclName);

        // Check if we found this Name in the parent interpreter
        if (!lookup_result.empty()) {
          // Do the import
          Decl *fromDeclContext = Decl::castFromDeclContext(I->second);
          ASTContext &from_ASTContext = fromDeclContext->getASTContext();

          Decl *toDeclContext = Decl::castFromDeclContext(childCurrentDeclContext);
          ASTContext &to_ASTContext = toDeclContext->getASTContext();

          if (Import(lookup_result, from_ASTContext, to_ASTContext,
                     childCurrentDeclContext, childDeclName, parentDeclName))
            return true;
        }
      }
      return false;
    }
} // end namespace cling
