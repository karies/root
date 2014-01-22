// @(#)root/metautils:$Id$
// Author: Paul Russo, 2009-10-06

/*************************************************************************
 * Copyright (C) 1995-2011, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//______________________________________________________________________________
//                                                                      //
// ROOT::TMetaUtils provides utility wrappers around                    //
// cling, the LLVM-based interpreter. It's an internal set of tools     //
// used by TCling and rootcling.                                        //
//                                                                      //
//______________________________________________________________________________

#include <algorithm>

#include "RConfigure.h"
#include "RConfig.h"
#include "Rtypes.h"
#include "compiledata.h"

#include "RStl.h"

#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <stdio.h>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Attr.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/Preprocessor.h"

#include "clang/Sema/Sema.h"

#include "cling/Interpreter/LookupHelper.h"

#include "llvm/Support/Path.h"
#include "llvm/Support/FileSystem.h"

#include "cling/Interpreter/Interpreter.h"

// Intentionally access non-public header ...
#include "../../../interpreter/llvm/src/tools/clang/lib/Sema/HackForDefaultTemplateArg.h"

#include "TMetaUtils.h"

int ROOT::TMetaUtils::gErrorIgnoreLevel = ROOT::TMetaUtils::kError;
// std::vector<ROOT::TMetaUtils::RConstructorType> gIoConstructorTypes;

namespace {

//______________________________________________________________________________
static clang::NestedNameSpecifier* AddDefaultParametersNNS(const clang::ASTContext& Ctx,
                                                           clang::NestedNameSpecifier* scope,
                                                           const cling::Interpreter &interpreter,
                                                           const ROOT::TMetaUtils::TNormalizedCtxt &normCtxt) {
   // Add default parameter to the scope if needed.

   if (!scope) return 0;

   const clang::Type* scope_type = scope->getAsType();
   if (scope_type) {
      // this is not a namespace, so we might need to desugar
      clang::NestedNameSpecifier* outer_scope = scope->getPrefix();
      if (outer_scope) {
         outer_scope = AddDefaultParametersNNS(Ctx, outer_scope, interpreter, normCtxt);
      }

      clang::QualType addDefault =
      ROOT::TMetaUtils::AddDefaultParameters(clang::QualType(scope_type,0), interpreter, normCtxt );
      // NOTE: Should check whether the type has changed or not.
      return clang::NestedNameSpecifier::Create(Ctx,outer_scope,
                                                false /* template keyword wanted */,
                                                addDefault.getTypePtr());
   }
   return scope;
                                                                             }
//______________________________________________________________________________
static bool CheckDefinition(const clang::CXXRecordDecl *cl, const clang::CXXRecordDecl *context)
{
   if (!cl->hasDefinition()) {
      if (context) {
         ROOT::TMetaUtils::Error("CheckDefinition",
                                 "Missing definition for class %s, please #include its header in the header of %s\n",
                                 cl->getName().str().c_str(), context->getName().str().c_str());
      } else {
         ROOT::TMetaUtils::Error("CheckDefinition",
                                 "Missing definition for class %s\n",
                                 cl->getName().str().c_str());
      }
      return false;
   }
   return true;
}

//______________________________________________________________________________
static int WriteNamespaceHeader(std::ostream &out, const clang::DeclContext *ctxt)
{
   // Write all the necessary opening part of the namespace and
   // return the number of closing brackets needed
   // For example for Space1::Space2
   // we write: namespace Space1 { namespace Space2 {
      // and return 2.

      int closing_brackets = 0;

      //fprintf(stderr,"DEBUG: in WriteNamespaceHeader for %s with %s\n",
      //    cl.Fullname(),namespace_obj.Fullname());
      if (ctxt && ctxt->isNamespace()) {
         closing_brackets = WriteNamespaceHeader(out,ctxt->getParent());
         for (int indent = 0; indent < closing_brackets; ++indent) {
            out << "   ";
         }
         const clang::NamespaceDecl *ns = llvm::dyn_cast<clang::NamespaceDecl>(ctxt);
         out << "namespace " << ns->getNameAsString() << " {" << std::endl;
         closing_brackets++;
         }

         return closing_brackets;
      }

//______________________________________________________________________________
static clang::NestedNameSpecifier* ReSubstTemplateArgNNS(const clang::ASTContext &Ctxt,
                                                         clang::NestedNameSpecifier *scope,
                                                         const clang::Type *instance)
{
   // Check if 'scope' or any of its template parameter was substituted when
   // instantiating the class template instance and replace it with the
   // partially sugared types we have from 'instance'.

   if (!scope) return 0;

   const clang::Type* scope_type = scope->getAsType();
   if (scope_type) {
      clang::NestedNameSpecifier* outer_scope = scope->getPrefix();
      if (outer_scope) {
         outer_scope = ReSubstTemplateArgNNS(Ctxt, outer_scope, instance);
      }
      clang::QualType substScope =
         ROOT::TMetaUtils::ReSubstTemplateArg(clang::QualType(scope_type,0), instance);
      // NOTE: Should check whether the type has changed or not.
      scope = clang::NestedNameSpecifier::Create(Ctxt,outer_scope,
                                                 false /* template keyword wanted */,
                                                 substScope.getTypePtr());
   }
   return scope;
}

//______________________________________________________________________________
static bool IsTypeInt(const clang::Type *type)
{
   const clang::BuiltinType * builtin = llvm::dyn_cast<clang::BuiltinType>(type->getCanonicalTypeInternal().getTypePtr());
   if (builtin) {
      return builtin->isInteger(); // builtin->getKind() == clang::BuiltinType::Int;
   } else {
      return false;
   }
}

//______________________________________________________________________________
static bool IsFieldDeclInt(const clang::FieldDecl *field)
{
   return IsTypeInt(field->getType().getTypePtr());
}

//______________________________________________________________________________
static const clang::FieldDecl *GetDataMemberFromAll(const clang::CXXRecordDecl &cl, const char *what)
{
   // Return a data member name 'what' in the class described by 'cl' if any.

   for(clang::RecordDecl::field_iterator field_iter = cl.field_begin(), end = cl.field_end();
       field_iter != end;
   ++field_iter){
      if (field_iter->getNameAsString() == what) {
         return *field_iter;
      }
   }
   return 0;

}

//______________________________________________________________________________
static bool CXXRecordDecl__FindOrdinaryMember(const clang::CXXBaseSpecifier *Specifier,
                                              clang::CXXBasePath &Path,
                                              void *Name
)
{
   clang::RecordDecl *BaseRecord = Specifier->getType()->getAs<clang::RecordType>()->getDecl();

   const clang::CXXRecordDecl *clxx = llvm::dyn_cast<clang::CXXRecordDecl>(BaseRecord);
   if (clxx == 0) return false;

   const clang::FieldDecl *found = GetDataMemberFromAll(*clxx,(const char*)Name);
   if (found) {
      // Humm, this is somewhat bad (well really bad), oh well.
      // Let's hope Paths never thinks it owns those (it should not as far as I can tell).
      clang::NamedDecl* NonConstFD = const_cast<clang::FieldDecl*>(found);
      clang::NamedDecl** BaseSpecFirstHack
      = reinterpret_cast<clang::NamedDecl**>(NonConstFD);
      Path.Decls = clang::DeclContextLookupResult(BaseSpecFirstHack, 1);
      return true;
   }
   //
   // This is inspired from CXXInheritance.cpp:
   /*
    *      RecordDecl *BaseRecord =
    *        Specifier->getType()->castAs<RecordType>()->getDecl();
    *
    *  const unsigned IDNS = clang::Decl::IDNS_Ordinary | clang::Decl::IDNS_Tag | clang::Decl::IDNS_Member;
    *  clang::DeclarationName N = clang::DeclarationName::getFromOpaquePtr(Name);
    *  for (Path.Decls = BaseRecord->lookup(N);
    *       Path.Decls.first != Path.Decls.second;
    *       ++Path.Decls.first) {
    *     if ((*Path.Decls.first)->isInIdentifierNamespace(IDNS))
    *        return true;
    }
    */
   return false;

}

//______________________________________________________________________________
static const clang::FieldDecl *GetDataMemberFromAllParents(const clang::CXXRecordDecl &cl, const char *what)
{
   // Return a data member name 'what' in any of the base classes of the class described by 'cl' if any.

   clang::CXXBasePaths Paths;
   Paths.setOrigin(const_cast<clang::CXXRecordDecl*>(&cl));
   if (cl.lookupInBases(&CXXRecordDecl__FindOrdinaryMember,
      (void*) const_cast<char*>(what),
                        Paths) )
   {
      clang::CXXBasePaths::paths_iterator iter = Paths.begin();
      if (iter != Paths.end()) {
         // See CXXRecordDecl__FindOrdinaryMember, this is, well, awkward.
         const clang::FieldDecl *found = (clang::FieldDecl *)iter->Decls.data();
         return found;
      }
   }
   return 0;
}

} // end of anonymous namespace

namespace ROOT{
   namespace TMetaUtils{

//______________________________________________________________________________
AnnotatedRecordDecl::AnnotatedRecordDecl(long index,
                                         const clang::RecordDecl *decl,
                                         bool rStreamerInfo,
                                         bool rNoStreamer,
                                         bool rRequestNoInputOperator,
                                         bool rRequestOnlyTClass,
                                         int rRequestedVersionNumber,
                                         const cling::Interpreter &interpreter,
                                         const TNormalizedCtxt &normCtxt) :
   fRuleIndex(index), fDecl(decl), fRequestStreamerInfo(rStreamerInfo), fRequestNoStreamer(rNoStreamer),
   fRequestNoInputOperator(rRequestNoInputOperator), fRequestOnlyTClass(rRequestOnlyTClass), fRequestedVersionNumber(rRequestedVersionNumber)
{
   // There is no requested type name.
   // Still let's normalized the actual name.

   TMetaUtils::GetNormalizedName(fNormalizedName, decl->getASTContext().getTypeDeclType(decl),interpreter,normCtxt);

}


//______________________________________________________________________________
AnnotatedRecordDecl::AnnotatedRecordDecl(long index,
                                         const clang::Type *requestedType,
                                         const clang::RecordDecl *decl,
                                         const char *requestName,
                                         bool rStreamerInfo,
                                         bool rNoStreamer,
                                         bool rRequestNoInputOperator,
                                         bool rRequestOnlyTClass,
                                         int rRequestVersionNumber,
                                         const cling::Interpreter &interpreter,
                                         const TNormalizedCtxt &normCtxt) :
   fRuleIndex(index), fDecl(decl), fRequestedName(""), fRequestStreamerInfo(rStreamerInfo), fRequestNoStreamer(rNoStreamer),
   fRequestNoInputOperator(rRequestNoInputOperator), fRequestOnlyTClass(rRequestOnlyTClass), fRequestedVersionNumber(rRequestVersionNumber)
{
   // Normalize the requested type name.

   // For comparison purposes.
   TClassEdit::TSplitType splitname1(requestName,(TClassEdit::EModType)(TClassEdit::kLong64 | TClassEdit::kDropStd));
   splitname1.ShortType( fRequestedName, TClassEdit::kDropAllDefault );

   TMetaUtils::GetNormalizedName( fNormalizedName, clang::QualType(requestedType,0), interpreter, normCtxt);

}

//______________________________________________________________________________
AnnotatedRecordDecl::AnnotatedRecordDecl(long index,
                                         const clang::RecordDecl *decl,
                                         const char *requestName,
                                         bool rStreamerInfo,
                                         bool rNoStreamer,
                                         bool rRequestNoInputOperator,
                                         bool rRequestOnlyTClass,
                                         int rRequestVersionNumber,
                                         const cling::Interpreter &interpreter,
                                         const TNormalizedCtxt &normCtxt) :
   fRuleIndex(index), fDecl(decl), fRequestedName(""), fRequestStreamerInfo(rStreamerInfo), fRequestNoStreamer(rNoStreamer), fRequestNoInputOperator(rRequestNoInputOperator), fRequestOnlyTClass(rRequestOnlyTClass), fRequestedVersionNumber(rRequestVersionNumber)
{
   // Normalize the requested name.

   // const clang::ClassTemplateSpecializationDecl *tmplt_specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl> (decl);
   // if (tmplt_specialization) {
   //    tmplt_specialization->getTemplateArgs ().data()->print(decl->getASTContext().getPrintingPolicy(),llvm::outs());
   //    llvm::outs() << "\n";
   // }
   // const char *current = requestName;
   // Strips spaces and std::
   if (requestName && requestName[0]) {
      TClassEdit::TSplitType splitname(requestName,(TClassEdit::EModType)(TClassEdit::kDropAllDefault | TClassEdit::kLong64 | TClassEdit::kDropStd));
      splitname.ShortType( fRequestedName, TClassEdit::kDropAllDefault | TClassEdit::kLong64 | TClassEdit::kDropStd );

      fNormalizedName = fRequestedName;
   } else {
      TMetaUtils::GetNormalizedName( fNormalizedName, decl->getASTContext().getTypeDeclType(decl),interpreter,normCtxt);
   }


}



//______________________________________________________________________________
TClingLookupHelper::TClingLookupHelper(cling::Interpreter &interpreter,
                                       TNormalizedCtxt &normCtxt):
   fInterpreter(&interpreter),fNormalizedCtxt(&normCtxt)
{

}

//______________________________________________________________________________
void TClingLookupHelper::GetPartiallyDesugaredName(std::string &nameLong)
{
   const cling::LookupHelper& lh = fInterpreter->getLookupHelper();
   clang::QualType t = lh.findType(nameLong);
   if (!t.isNull()) {
      clang::QualType dest = cling::utils::Transform::GetPartiallyDesugaredType(fInterpreter->getCI()->getASTContext(), t, fNormalizedCtxt->GetConfig(), true /* fully qualify */);
      if (!dest.isNull() && (dest != t))
         dest.getAsStringInternal(nameLong, fInterpreter->getCI()->getASTContext().getPrintingPolicy());
   }
}

//______________________________________________________________________________
bool TClingLookupHelper::IsAlreadyPartiallyDesugaredName(const std::string &nondef,
                                                         const std::string &nameLong)
{
   const cling::LookupHelper& lh = fInterpreter->getLookupHelper();
   clang::QualType t = lh.findType(nondef.c_str());
   if (!t.isNull()) {
      clang::QualType dest = cling::utils::Transform::GetPartiallyDesugaredType(fInterpreter->getCI()->getASTContext(), t, fNormalizedCtxt->GetConfig(), true /* fully qualify */);
      if (!dest.isNull() && (dest != t) &&
          nameLong == t.getAsString(fInterpreter->getCI()->getASTContext().getPrintingPolicy()))
         return true;
   }
   return false;
}

//______________________________________________________________________________
bool TClingLookupHelper::IsDeclaredScope(const std::string &base)
{
   const cling::LookupHelper& lh = fInterpreter->getLookupHelper();
   if (!lh.findScope(base.c_str(), 0)) {
      // the nesting namespace is not declared
      return false;
   }
   return true;
}

//______________________________________________________________________________
bool TClingLookupHelper::GetPartiallyDesugaredNameWithScopeHandling(const std::string &tname,
                                                                    std::string &result)
{
   const cling::LookupHelper& lh = fInterpreter->getLookupHelper();
   clang::QualType t = lh.findType(tname.c_str());
   if (!t.isNull()) {
      clang::QualType dest = cling::utils::Transform::GetPartiallyDesugaredType(fInterpreter->getCI()->getASTContext(), t, fNormalizedCtxt->GetConfig(), true /* fully qualify */);
      if (!dest.isNull() && dest != t) {
         clang::PrintingPolicy policy(fInterpreter->getCI()->getASTContext().getPrintingPolicy());
         policy.SuppressTagKeyword = true; // Never get the class or struct keyword
         policy.SuppressScope = true;      // Force the scope to be coming from a clang::ElaboratedType.
         // The scope suppression is required for getting rid of the anonymous part of the name of a class defined in an anonymous namespace.
         // This gives us more control vs not using the clang::ElaboratedType and relying on the Policy.SuppressUnwrittenScope which would
         // strip both the anonymous and the inline namespace names (and we probably do not want the later to be suppressed).
         dest.getAsStringInternal(result, policy);
         // Strip the std::
         if (strncmp(result.c_str(), "std::", 5) == 0) {
            result = result.substr(5);
         }
         return true;
      }
   }
   return false;
}


   } // end namespace ROOT
} // end namespace TMetaUtils

//______________________________________________________________________________
ROOT::TMetaUtils::TNormalizedCtxt::TNormalizedCtxt(const cling::LookupHelper &lh)
{
   // Initialize the list of typedef to keep (i.e. make them opaque for normalization)
   // and the list of typedef whose semantic is different from their underlying type
   // (Double32_t and Float16_t).
   // This might be specific to an interpreter.

   clang::QualType toSkip = lh.findType("Double32_t");
   if (!toSkip.isNull()) {
      fConfig.m_toSkip.insert(toSkip.getTypePtr());
      fTypeWithAlternative.insert(toSkip.getTypePtr());
   }
   toSkip = lh.findType("Float16_t");
   if (!toSkip.isNull()) {
      fConfig.m_toSkip.insert(toSkip.getTypePtr());
      fTypeWithAlternative.insert(toSkip.getTypePtr());
   }
   toSkip = lh.findType("Long64_t");
   if (!toSkip.isNull()) fConfig.m_toSkip.insert(toSkip.getTypePtr());
   toSkip = lh.findType("ULong64_t");
   if (!toSkip.isNull()) fConfig.m_toSkip.insert(toSkip.getTypePtr());
   toSkip = lh.findType("string");
   if (!toSkip.isNull()) fConfig.m_toSkip.insert(toSkip.getTypePtr());
   toSkip = lh.findType("std::string");
   if (!toSkip.isNull()) {
      fConfig.m_toSkip.insert(toSkip.getTypePtr());

      clang::QualType canon = toSkip->getCanonicalTypeInternal();
      fConfig.m_toReplace.insert(std::make_pair(canon.getTypePtr(),toSkip.getTypePtr()));
   }
}

//______________________________________________________________________________
inline bool IsTemplate(const clang::Decl &cl)
{
   return (cl.getKind() == clang::Decl::ClassTemplatePartialSpecialization
           || cl.getKind() == clang::Decl::ClassTemplateSpecialization);
}


//______________________________________________________________________________
bool ROOT::TMetaUtils::ClassInfo__HasMethod(const clang::RecordDecl *cl, const char* name)
{
   const clang::CXXRecordDecl* CRD = llvm::dyn_cast<clang::CXXRecordDecl>(cl);
   if (!CRD) {
      return false;
   }
   std::string given_name(name);
   for (
        clang::CXXRecordDecl::method_iterator M = CRD->method_begin(),
        MEnd = CRD->method_end();
        M != MEnd;
        ++M
        )
   {
      if (M->getNameAsString() == given_name) {
         return true;
      }
   }
   return false;
}

//______________________________________________________________________________
const clang::CXXRecordDecl *ROOT::TMetaUtils::ScopeSearch(const char *name, const cling::Interpreter &interp, const clang::Type** resultType)
{
   // Return the scope corresponding to 'name' or std::'name'
   const cling::LookupHelper& lh = interp.getLookupHelper();
   const clang::CXXRecordDecl *result = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(lh.findScope(name,resultType));
   if (!result) {
      std::string std_name("std::");
      std_name += name;
      result = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(lh.findScope(std_name,resultType));
   }
   return result;
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::IsBase(const clang::CXXRecordDecl *cl, const clang::CXXRecordDecl *base,
                                 const clang::CXXRecordDecl *context /*=0*/)
{
   if (!cl || !base) {
      return false;
   }
   if (!CheckDefinition(cl, context) || !CheckDefinition(base, context)) {
      return false;
   }

   if (!base->hasDefinition()) {
      ROOT::TMetaUtils::Error("IsBase", "Missing definition for class %s\n", base->getName().str().c_str());
      return false;
   }
   return cl->isDerivedFrom(base);
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::IsBase(const clang::FieldDecl &m, const char* basename, const cling::Interpreter &interp)
{
   const clang::CXXRecordDecl* CRD = llvm::dyn_cast<clang::CXXRecordDecl>(ROOT::TMetaUtils::GetUnderlyingRecordDecl(m.getType()));
   if (!CRD) {
      return false;
   }

   const clang::NamedDecl *base = ScopeSearch(basename, interp);

   if (base) {
      return IsBase(CRD, llvm::dyn_cast<clang::CXXRecordDecl>( base ),
                       llvm::dyn_cast<clang::CXXRecordDecl>(m.getDeclContext()));
   }
   return false;
}

//______________________________________________________________________________
int ROOT::TMetaUtils::ElementStreamer(std::ostream& finalString,
                                      const clang::NamedDecl &forcontext,
                                      const clang::QualType &qti,
                                      const char *R__t,int rwmode,
                                      const cling::Interpreter &interp,
                                      const char *tcl)
{

   static const clang::CXXRecordDecl *TObject_decl = ROOT::TMetaUtils::ScopeSearch("TObject", interp);
   enum {
      kBIT_ISTOBJECT     = 0x10000000,
      kBIT_HASSTREAMER   = 0x20000000,
      kBIT_ISSTRING      = 0x40000000,

      kBIT_ISPOINTER     = 0x00001000,
      kBIT_ISFUNDAMENTAL = 0x00000020,
      kBIT_ISENUM        = 0x00000008
   };

   const clang::Type &ti( * qti.getTypePtr() );
   std::string tiName;
   ROOT::TMetaUtils::GetQualifiedName(tiName, clang::QualType(&ti,0), forcontext);

   std::string objType(ROOT::TMetaUtils::ShortTypeName(tiName.c_str()));

   const clang::Type *rawtype = ROOT::TMetaUtils::GetUnderlyingType(clang::QualType(&ti,0));
   std::string rawname;
   ROOT::TMetaUtils::GetQualifiedName(rawname, clang::QualType(rawtype,0), forcontext);

   clang::CXXRecordDecl *cxxtype = rawtype->getAsCXXRecordDecl() ;
   int isStre = cxxtype && ROOT::TMetaUtils::ClassInfo__HasMethod(cxxtype,"Streamer");
   int isTObj = cxxtype && (IsBase(cxxtype,TObject_decl) || rawname == "TObject");

   long kase = 0;

   if (ti.isPointerType())           kase |= kBIT_ISPOINTER;
   if (rawtype->isFundamentalType()) kase |= kBIT_ISFUNDAMENTAL;
   if (rawtype->isEnumeralType())    kase |= kBIT_ISENUM;


   if (isTObj)              kase |= kBIT_ISTOBJECT;
   if (isStre)              kase |= kBIT_HASSTREAMER;
   if (tiName == "string")  kase |= kBIT_ISSTRING;
   if (tiName == "string*") kase |= kBIT_ISSTRING;


   if (tcl == 0) {
      tcl = " internal error in rootcling ";
   }
   //    if (strcmp(objType,"string")==0) RStl::Instance().GenerateTClassFor( "string", interp, normCtxt  );

   if (rwmode == 0) {  //Read mode

      if (R__t) finalString << "            " << tiName << " " << R__t << ";" << std::endl;
      switch (kase) {

      case kBIT_ISFUNDAMENTAL:
         if (!R__t)  return 0;
         finalString << "            R__b >> " << R__t << ";" << std::endl;
         break;

      case kBIT_ISPOINTER|kBIT_ISTOBJECT|kBIT_HASSTREAMER:
         if (!R__t)  return 1;
         finalString << "            " << R__t << " = (" << tiName << ")R__b.ReadObjectAny(" << tcl << ");"  << std::endl;
         break;

      case kBIT_ISENUM:
         if (!R__t)  return 0;
         //             fprintf(fp, "            R__b >> (Int_t&)%s;\n",R__t);
         // On some platforms enums and not 'Int_t' and casting to a reference to Int_t
         // induces the silent creation of a temporary which is 'filled' __instead of__
         // the desired enum.  So we need to take it one step at a time.
         finalString << "            Int_t readtemp;" << std::endl
                       << "            R__b >> readtemp;" << std::endl
                       << "            " << R__t << " = static_cast<" << tiName << ">(readtemp);" << std::endl;
         break;

      case kBIT_HASSTREAMER:
      case kBIT_HASSTREAMER|kBIT_ISTOBJECT:
         if (!R__t)  return 0;
         finalString << "            " << R__t << ".Streamer(R__b);" << std::endl;
         break;

      case kBIT_HASSTREAMER|kBIT_ISPOINTER:
         if (!R__t)  return 1;
         //fprintf(fp, "            fprintf(stderr,\"info is %%p %%d\\n\",R__b.GetInfo(),R__b.GetInfo()?R__b.GetInfo()->GetOldVersion():-1);\n");
         finalString << "            if (R__b.GetInfo() && R__b.GetInfo()->GetOldVersion()<=3) {" << std::endl;
         if (cxxtype && cxxtype->isAbstract()) {
            finalString << "               R__ASSERT(0);// " << objType << " is abstract. We assume that older file could not be produced using this streaming method." << std::endl;
         } else {
            finalString << "               " << R__t << " = new " << objType << ";" << std::endl
                          << "               " << R__t << "->Streamer(R__b);" << std::endl;
         }
         finalString << "            } else {" << std::endl
                       << "               " << R__t << " = (" << tiName << ")R__b.ReadObjectAny(" << tcl << ");" << std::endl
                       << "            }" << std::endl;
         break;

      case kBIT_ISSTRING:
         if (!R__t)  return 0;
         finalString << "            {TString R__str;" << std::endl
                       << "             R__str.Streamer(R__b);" << std::endl
                       << "             " << R__t << " = R__str.Data();}" << std::endl;
         break;

      case kBIT_ISSTRING|kBIT_ISPOINTER:
         if (!R__t)  return 0;
         finalString << "            {TString R__str;"  << std::endl
                       << "             R__str.Streamer(R__b);" << std::endl
                       << "             " << R__t << " = new string(R__str.Data());}" << std::endl;
         break;

      case kBIT_ISPOINTER:
         if (!R__t)  return 1;
         finalString << "            " << R__t << " = (" << tiName << ")R__b.ReadObjectAny(" << tcl << ");" << std::endl;
         break;

      default:
         if (!R__t) return 1;
         finalString << "            R__b.StreamObject(&" << R__t << "," << tcl << ");" << std::endl;
         break;
      }

   } else {     //Write case

      switch (kase) {

      case kBIT_ISFUNDAMENTAL:
      case kBIT_ISPOINTER|kBIT_ISTOBJECT|kBIT_HASSTREAMER:
         if (!R__t)  return 0;
         finalString << "            R__b << " << R__t << ";" << std::endl;
         break;

      case kBIT_ISENUM:
         if (!R__t)  return 0;
         finalString << "            {  void *ptr_enum = (void*)&" << R__t << ";\n";
         finalString << "               R__b >> *reinterpret_cast<Int_t*>(ptr_enum); }" << std::endl;
         break;

      case kBIT_HASSTREAMER:
      case kBIT_HASSTREAMER|kBIT_ISTOBJECT:
         if (!R__t)  return 0;
         finalString << "            ((" << objType << "&)" << R__t << ").Streamer(R__b);" << std::endl;
         break;

      case kBIT_HASSTREAMER|kBIT_ISPOINTER:
         if (!R__t)  return 1;
         finalString << "            R__b.WriteObjectAny(" << R__t << "," << tcl << ");" << std::endl;
         break;

      case kBIT_ISSTRING:
         if (!R__t)  return 0;
         finalString << "            {TString R__str(" << R__t << ".c_str());" << std::endl
                       << "             R__str.Streamer(R__b);};" << std::endl;
         break;

      case kBIT_ISSTRING|kBIT_ISPOINTER:
         if (!R__t)  return 0;
         finalString << "            {TString R__str(" << R__t << "->c_str());" << std::endl
                       << "             R__str.Streamer(R__b);}" << std::endl;
         break;

      case kBIT_ISPOINTER:
         if (!R__t)  return 1;
         finalString << "            R__b.WriteObjectAny(" << R__t << "," << tcl <<");" << std::endl;
         break;

      default:
         if (!R__t)  return 1;
         finalString << "            R__b.StreamObject((" << objType << "*)&" << R__t << "," << tcl << ");" << std::endl;
         break;
      }
   }
   return 0;
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::CheckConstructor(const clang::CXXRecordDecl *cl,
                                        const RConstructorType &ioctortype)
{
   const char *arg = ioctortype.GetName();
   if ( (arg == 0 || arg[0] == '\0') && !cl->hasUserDeclaredConstructor() ) {
      return true;
   }

   if (ioctortype.GetType() ==0 && (arg == 0 || arg[0] == '\0')) {
      // We are looking for a constructor with zero non-default arguments.

      for(clang::CXXRecordDecl::ctor_iterator iter = cl->ctor_begin(), end = cl->ctor_end();
          iter != end;
          ++iter)
      {
         if (iter->getAccess() != clang::AS_public)
            continue;
         // We can reach this constructor.

         if (iter->getNumParams() == 0) {
            return true;
         }
         if ( (*iter->param_begin())->hasDefaultArg()) {
            return true;
         }
      } // For each constructor.
   }
   else {
      for(clang::CXXRecordDecl::ctor_iterator iter = cl->ctor_begin(), end = cl->ctor_end();
          iter != end;
          ++iter)
      {
         if (iter->getAccess() != clang::AS_public)
            continue;

         // We can reach this constructor.
         if (iter->getNumParams() == 1) {
            clang::QualType argType( (*iter->param_begin())->getType() );
            argType = argType.getDesugaredType(cl->getASTContext());
            if (argType->isPointerType()) {
               argType = argType->getPointeeType();
               argType = argType.getDesugaredType(cl->getASTContext());

               const clang::CXXRecordDecl *argDecl = argType->getAsCXXRecordDecl();
               if (argDecl && ioctortype.GetType()) {
                  if (argDecl->getCanonicalDecl() == ioctortype.GetType()->getCanonicalDecl()) {
                     return true;
                  }
               } else {
                  std::string realArg = argType.getAsString();
                  std::string clarg("class ");
                  clarg += arg;
                  if (realArg == clarg) {
                     return true;

                  }
               }
            }
         } // has one argument.
      } // for each constructor
   }

   return false;
}


//______________________________________________________________________________
const clang::CXXMethodDecl *GetMethodWithProto(const clang::Decl* cinfo,
                                               const char *method, const char *proto, const cling::Interpreter &interp)
{
   const clang::FunctionDecl* funcD
   = interp.getLookupHelper().findFunctionProto(cinfo, method, proto);
   if (funcD) {
      return llvm::dyn_cast<const clang::CXXMethodDecl>(funcD);
   }
   return 0;
}


//______________________________________________________________________________
namespace ROOT {
   namespace TMetaUtils {
      RConstructorType::RConstructorType(const char *type_of_arg, const cling::Interpreter &interp) : fArgTypeName(type_of_arg),fArgType(0)
      {
         const cling::LookupHelper& lh = interp.getLookupHelper();
         // We can not use findScope since the type we are given are usually,
         // only forward declared (and findScope explicitly reject them).
         clang::QualType instanceType = lh.findType(type_of_arg);
         if (!instanceType.isNull())
            fArgType = instanceType->getAsCXXRecordDecl();
      }
      const char *RConstructorType::GetName() const { return fArgTypeName.c_str(); }
      const clang::CXXRecordDecl *RConstructorType::GetType() const { return fArgType; }
   }
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::HasIOConstructor(const clang::CXXRecordDecl *cl,
                                        std::string& arg,
                                        const RConstructorTypes& ctorTypes,
                                        const cling::Interpreter &interp)
{
   // return true if we can find an constructor calleable without any arguments
   // or with one the IOCtor special types.

   bool result = false;

   if (cl->isAbstract()) return false;

   for(RConstructorTypes::const_iterator ctorTypeIt=ctorTypes.begin();
       ctorTypeIt!=ctorTypes.end();++ctorTypeIt){
      std::string proto( ctorTypeIt->GetName() );
      int extra = (proto.size()==0) ? 0 : 1;
      if (extra==0) {
         // Looking for default constructor
         result = true;
      } else {
         proto += " *";
      }

      result = ROOT::TMetaUtils::CheckConstructor(cl,*ctorTypeIt);
      if (result && extra) {
         arg = "( (";
         arg += proto;
         arg += ")0 )";
      }

      // Check for private operator new
      if (result) {
         const char *name = "operator new";
         proto = "size_t";
         const clang::CXXMethodDecl *method = GetMethodWithProto(cl,name,proto.c_str(), interp);
         if (method && method->getAccess() != clang::AS_public) {
            result = false;
         }
         if (result) return true;
      }
   }
   return result;
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::NeedDestructor(const clang::CXXRecordDecl *cl)
{
   if (!cl) return false;

   if (cl->hasUserDeclaredDestructor()) {

      clang::CXXDestructorDecl *dest = cl->getDestructor();
      if (dest) {
         return (dest->getAccess() == clang::AS_public);
      } else {
         return true; // no destructor, so let's assume it means default?
      }
   }
   return true;
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::CheckPublicFuncWithProto(const clang::CXXRecordDecl *cl,
                                                 const char *methodname,
                                                const char *proto,
                                                const cling::Interpreter &interp)
{
   // Return true, if the function (defined by the name and prototype) exists and is public

   const clang::CXXMethodDecl *method = GetMethodWithProto(cl,methodname,proto, interp);
   return (method && method->getAccess() == clang::AS_public);
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::HasDirectoryAutoAdd(const clang::CXXRecordDecl *cl, const cling::Interpreter &interp)
{
   // Return true if the class has a method DirectoryAutoAdd(TDirectory *)

   // Detect if the class has a DirectoryAutoAdd

   // Detect if the class or one of its parent has a DirectoryAutoAdd
   const char *proto = "TDirectory*";
   const char *name = "DirectoryAutoAdd";

   return CheckPublicFuncWithProto(cl,name,proto,interp);
}


//______________________________________________________________________________
bool ROOT::TMetaUtils::HasNewMerge(const clang::CXXRecordDecl *cl, const cling::Interpreter &interp)
{
   // Return true if the class has a method Merge(TCollection*,TFileMergeInfo*)

   // Detect if the class has a 'new' Merge function.

   // Detect if the class or one of its parent has a DirectoryAutoAdd
   const char *proto = "TCollection*,TFileMergeInfo*";
   const char *name = "Merge";

   return CheckPublicFuncWithProto(cl,name,proto,interp);
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::HasOldMerge(const clang::CXXRecordDecl *cl, const cling::Interpreter &interp)
{
   // Return true if the class has a method Merge(TCollection*)

   // Detect if the class has an old fashion Merge function.

   // Detect if the class or one of its parent has a DirectoryAutoAdd
   const char *proto = "TCollection*";
   const char *name = "Merge";

   return CheckPublicFuncWithProto(cl,name,proto, interp);
}


//______________________________________________________________________________
bool ROOT::TMetaUtils::HasResetAfterMerge(const clang::CXXRecordDecl *cl, const cling::Interpreter &interp)
{
   // Return true if the class has a method ResetAfterMerge(TFileMergeInfo *)

   // Detect if the class has a 'new' Merge function.
   // bool hasMethod = cl.HasMethod("DirectoryAutoAdd");

   // Detect if the class or one of its parent has a DirectoryAutoAdd
   const char *proto = "TFileMergeInfo*";
   const char *name = "ResetAfterMerge";

   return CheckPublicFuncWithProto(cl,name,proto, interp);
}


//______________________________________________________________________________
bool ROOT::TMetaUtils::HasCustomStreamerMemberFunction(const AnnotatedRecordDecl &cl,
                                                       const clang::CXXRecordDecl* clxx,
                                                       const cling::Interpreter &interp,
                                                       const TNormalizedCtxt &normCtxt)
{
   // Return true if the class has a custom member function streamer.

   static const char *proto = "TBuffer&";

   const clang::CXXMethodDecl *method = GetMethodWithProto(clxx,"Streamer",proto, interp);
   const clang::DeclContext *clxx_as_context = llvm::dyn_cast<clang::DeclContext>(clxx);

   return (method && method->getDeclContext() == clxx_as_context && ( cl.RequestNoStreamer() || !cl.RequestStreamerInfo()));
}

//______________________________________________________________________________
void ROOT::TMetaUtils::GetQualifiedName(std::string &qual_name, const clang::QualType &type, const clang::NamedDecl &forcontext)
{
   // Main implementation relying on GetFullyQualifiedTypeName
   // All other GetQualifiedName functions leverage this one except the
   // one for namespaces.
   ROOT::TMetaUtils::GetFullyQualifiedTypeName(qual_name, type, forcontext.getASTContext());
}

//----
std::string ROOT::TMetaUtils::GetQualifiedName(const clang::QualType &type, const clang::NamedDecl &forcontext)
{
   std::string result;
   ROOT::TMetaUtils::GetQualifiedName(result,
                                      type,
                                      forcontext);
   return result;
}


//______________________________________________________________________________
void  ROOT::TMetaUtils::GetQualifiedName(std::string& qual_type, const clang::Type &type, const clang::NamedDecl &forcontext)
{
   clang::QualType qualType(&type,0);
   ROOT::TMetaUtils::GetQualifiedName(qual_type,
                                      qualType,
                                      forcontext);
}

//---
std::string ROOT::TMetaUtils::GetQualifiedName(const clang::Type &type, const clang::NamedDecl &forcontext)
{
   std::string result;
   ROOT::TMetaUtils::GetQualifiedName(result,
                                      type,
                                      forcontext);
   return result;
}

//______________________________________________________________________________
void ROOT::TMetaUtils::GetQualifiedName(std::string &qual_name, const clang::NamespaceDecl &cl)
{
   // This implementation does not rely on GetFullyQualifiedTypeName
   // It is done for namespaces, no type involved.
   llvm::raw_string_ostream stream(qual_name);
   clang::PrintingPolicy policy( cl.getASTContext().getPrintingPolicy() );
   policy.SuppressTagKeyword = true; // Never get the class or struct keyword
   policy.SuppressUnwrittenScope = true; // Don't write the inline or anonymous namespace names.

   cl.getNameForDiagnostic(stream,policy,true);
   stream.flush(); // flush to string.

   if ( qual_name ==  "<anonymous " ) {
      size_t pos = qual_name.find(':');
      qual_name.erase(0,pos+2);
   }
}

//----
std::string ROOT::TMetaUtils::GetQualifiedName(const clang::NamespaceDecl &cl){
   std::string result;
   ROOT::TMetaUtils::GetQualifiedName(result, cl);
   return result;
}

//______________________________________________________________________________
void ROOT::TMetaUtils::GetQualifiedName(std::string &qual_name, const clang::RecordDecl &recordDecl)
{
   const clang::Type* declType ( recordDecl.getTypeForDecl() );
   clang::QualType qualType(declType,0);
   ROOT::TMetaUtils::GetQualifiedName(qual_name,
                                      qualType,
                                      recordDecl);
}

//----
std::string ROOT::TMetaUtils::GetQualifiedName(const clang::RecordDecl &recordDecl)
{
   std::string result;
   ROOT::TMetaUtils::GetQualifiedName(result,recordDecl);
   return result;
}

//______________________________________________________________________________
void ROOT::TMetaUtils::GetQualifiedName(std::string &qual_name, const ROOT::TMetaUtils::AnnotatedRecordDecl &annotated)
{
   ROOT::TMetaUtils::GetQualifiedName(qual_name, *annotated.GetRecordDecl());
}

//----
std::string ROOT::TMetaUtils::GetQualifiedName(const AnnotatedRecordDecl &annotated)
{
   std::string result;
   ROOT::TMetaUtils::GetQualifiedName(result, annotated);
   return result;
}

//______________________________________________________________________________
void ROOT::TMetaUtils::CreateNameTypeMap(const clang::CXXRecordDecl &cl, ROOT::MembersTypeMap_t& nameType)
{
   // Create the data member name-type map for given class

   std::stringstream dims;
   std::string typenameStr;

   const clang::ASTContext& astContext =  cl.getASTContext();

   // Loop over the non static data member.
   for(clang::RecordDecl::field_iterator field_iter = cl.field_begin(), end = cl.field_end();
       field_iter != end;
       ++field_iter){
      // The CINT based code was filtering away static variables (they are not part of
      // the list starting with field_begin in clang), and const enums (which should
      // also not be part of this list).
      // It was also filtering out the 'G__virtualinfo' artificial member.

      typenameStr.clear();
      dims.str("");
      dims.clear();

      clang::QualType fieldType(field_iter->getType());
      if (fieldType->isConstantArrayType()) {
         const clang::ConstantArrayType *arrayType = llvm::dyn_cast<clang::ConstantArrayType>(fieldType.getTypePtr());
         while (arrayType) {
            dims << "[" << arrayType->getSize().getLimitedValue() << "]";
            fieldType = arrayType->getElementType();
            arrayType = llvm::dyn_cast<clang::ConstantArrayType>(arrayType->getArrayElementTypeNoTypeQual());
         }
      }

      GetFullyQualifiedTypeName(typenameStr, fieldType, astContext);
      nameType[field_iter->getName().str()] = ROOT::TSchemaType(typenameStr.c_str(),dims.str().c_str());
   }

   // And now the base classes
   // We also need to look at the base classes.
   for(clang::CXXRecordDecl::base_class_const_iterator iter = cl.bases_begin(), end = cl.bases_end();
       iter != end;
       ++iter){
      std::string basename( iter->getType()->getAsCXXRecordDecl()->getNameAsString() ); // Intentionally using only the unqualified name.
      nameType[basename] = ROOT::TSchemaType(basename.c_str(),"");
   }
}

//______________________________________________________________________________
const clang::FunctionDecl *ROOT::TMetaUtils::GetFuncWithProto(const clang::Decl* cinfo,
                                                              const char *method,
                                                              const char *proto,
                                                              const cling::Interpreter &interp)
{
   return interp.getLookupHelper().findFunctionProto(cinfo, method, proto);
}

//______________________________________________________________________________
long ROOT::TMetaUtils::GetLineNumber(const clang::Decl *decl)
{
   // It looks like the template specialization decl actually contains _less_ information
   // on the location of the code than the decl (in case where there is forward declaration,
   // that is what the specialization points to.
   //
   // const clang::CXXRecordDecl* clxx = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
   // if (clxx) {
   //    switch(clxx->getTemplateSpecializationKind()) {
   //       case clang::TSK_Undeclared:
   //          // We want the default behavior
   //          break;
   //       case clang::TSK_ExplicitInstantiationDeclaration:
   //       case clang::TSK_ExplicitInstantiationDefinition:
   //       case clang::TSK_ImplicitInstantiation: {
   //          // We want the location of the template declaration:
   //          const clang::ClassTemplateSpecializationDecl *tmplt_specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl> (clxx);
   //          if (tmplt_specialization) {
   //             return GetLineNumber(const_cast< clang::ClassTemplateSpecializationDecl *>(tmplt_specialization)->getSpecializedTemplate());
   //          }
   //          break;
   //       }
   //       case clang::TSK_ExplicitSpecialization:
   //          // We want the default behavior
   //          break;
   //       default:
   //          break;
   //    }
   // }
   clang::SourceLocation sourceLocation = decl->getLocation();
   clang::SourceManager& sourceManager = decl->getASTContext().getSourceManager();

  if (!sourceLocation.isValid() ) {
      return -1;
   }

   if (!sourceLocation.isFileID()) {
      sourceLocation = sourceManager.getExpansionRange(sourceLocation).second;
   }

   if (sourceLocation.isValid() && sourceLocation.isFileID()) {
      return sourceManager.getLineNumber(sourceManager.getFileID(sourceLocation),sourceManager.getFileOffset(sourceLocation));
   }
   else {
      return -1;
   }
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::hasOpaqueTypedef(clang::QualType instanceType, const ROOT::TMetaUtils::TNormalizedCtxt &normCtxt)
{
   // Return true if the type is a Double32_t or Float16_t or
   // is a instance template that depends on Double32_t or Float16_t.

   while (llvm::isa<clang::PointerType>(instanceType.getTypePtr())
       || llvm::isa<clang::ReferenceType>(instanceType.getTypePtr()))
   {
      instanceType = instanceType->getPointeeType();
   }

   const clang::ElaboratedType* etype
      = llvm::dyn_cast<clang::ElaboratedType>(instanceType.getTypePtr());
   if (etype) {
      instanceType = clang::QualType(etype->getNamedType().getTypePtr(),0);
   }

   // There is no typedef to worried about, except for the opaque ones.

   // Technically we should probably used our own list with just
   // Double32_t and Float16_t
   if (normCtxt.GetTypeWithAlternative().count(instanceType.getTypePtr())) {
      return true;
   }


   bool result = false;
   const clang::CXXRecordDecl* clxx = instanceType->getAsCXXRecordDecl();
   if (clxx && clxx->getTemplateSpecializationKind() != clang::TSK_Undeclared) {
      // do the template thing.
      const clang::TemplateSpecializationType* TST
      = llvm::dyn_cast<const clang::TemplateSpecializationType>(instanceType.getTypePtr());
      if (TST==0) {
   //         std::string type_name;
   //         type_name =  GetQualifiedName( instanceType, *clxx );
   //         fprintf(stderr,"ERROR: Could not findS TST for %s\n",type_name.c_str());
         return false;
      }
      for(clang::TemplateSpecializationType::iterator
          I = TST->begin(), E = TST->end();
          I!=E; ++I)
      {
         if (I->getKind() == clang::TemplateArgument::Type) {
   //            std::string arg;
   //            arg = GetQualifiedName( I->getAsType(), *clxx );
   //            fprintf(stderr,"DEBUG: looking at %s\n", arg.c_str());
            result |= ROOT::TMetaUtils::hasOpaqueTypedef(I->getAsType(), normCtxt);
         }
      }
   }
   return result;
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::hasOpaqueTypedef(const AnnotatedRecordDecl &cl,
                                        const cling::Interpreter &interp,
                                        const TNormalizedCtxt &normCtxt)
{
   // Return true if any of the argument is or contains a double32.

   const clang::CXXRecordDecl* clxx =  llvm::dyn_cast<clang::CXXRecordDecl>(cl.GetRecordDecl());
   if (clxx->getTemplateSpecializationKind() == clang::TSK_Undeclared) return 0;

   clang::QualType instanceType = interp.getLookupHelper().findType(cl.GetNormalizedName());
   if (instanceType.isNull()) {
      //Error(0,"Could not find the clang::Type for %s\n",cl.GetNormalizedName());
      return false;
   } else {
      return ROOT::TMetaUtils::hasOpaqueTypedef(instanceType, normCtxt);
   }
}

//______________________________________________________________________________
int ROOT::TMetaUtils::extractAttrString(clang::Attr* attribute, std::string& attrString)
{
   // Extract attr string
   clang::AnnotateAttr* annAttr = clang::dyn_cast<clang::AnnotateAttr>(attribute);
   if (!annAttr) {
      //TMetaUtils::Error(0,"Could not cast Attribute to AnnotatedAttribute\n");
      return 1;
   }
   attrString = annAttr->getAnnotation();
   return 0;
}

//______________________________________________________________________________
int ROOT::TMetaUtils::extractPropertyNameValFromString(const std::string attributeStr,std::string& attrName, std::string& attrValue)
{

   // if separator found, extract name and value
   size_t substrFound (attributeStr.find(ROOT::TMetaUtils::PropertyNameValSeparator));
   if (substrFound==std::string::npos) {
      //TMetaUtils::Error(0,"Could not find property name-value separator (%s)\n",ROOT::TMetaUtils::PropertyNameValSeparator.c_str());
      return 1;
   }
   size_t EndPart1 = attributeStr.find_first_of(ROOT::TMetaUtils::PropertyNameValSeparator)  ;
   attrName = attributeStr.substr(0, EndPart1);
   const int separatorLength(ROOT::TMetaUtils::PropertyNameValSeparator.size());
   attrValue = attributeStr.substr(EndPart1 + separatorLength);
   return 0;
}

//______________________________________________________________________________
int ROOT::TMetaUtils::extractPropertyNameVal(clang::Attr* attribute, std::string& attrName, std::string& attrValue)
{
   std::string attrString;
   int ret = ROOT::TMetaUtils::extractAttrString(attribute, attrString);
   if (0!=ret) return ret;
   return ROOT::TMetaUtils::extractPropertyNameValFromString(attrString, attrName,attrValue);
}

//______________________________________________________________________________
void ROOT::TMetaUtils::WriteClassInit(std::ostream& finalString,
                                      const AnnotatedRecordDecl &cl,
                                      const clang::CXXRecordDecl *decl,
                                      const cling::Interpreter &interp,
                                      const TNormalizedCtxt &normCtxt,
                                      const RConstructorTypes& ctorTypes,
                                      bool& needCollectionProxy)
{
   // FIXME: a function of ~300 lines!
   std::string classname = TClassEdit::GetLong64_Name(cl.GetNormalizedName());

   std::string mappedname;
   ROOT::TMetaUtils::GetCppName(mappedname,classname.c_str());
   std::string csymbol = classname;
   std::string args;

   if ( ! TClassEdit::IsStdClass( classname.c_str() ) ) {

      // Prefix the full class name with '::' except for the STL
      // containers and std::string.  This is to request the
      // real class instead of the class in the namespace ROOT::Shadow
      csymbol.insert(0,"::");
   }

   int stl = TClassEdit::IsSTLCont(classname.c_str());
   bool bset = TClassEdit::IsSTLBitset(classname.c_str());

   bool isStd = TMetaUtils::IsStdClass(*decl);
   const cling::LookupHelper& lh = interp.getLookupHelper();
   bool isString = TMetaUtils::IsOfType(*decl,"std::string",lh);

   bool isStdNotString = isStd && !isString;

   finalString << "namespace ROOT {" << "\n";

   if (!ClassInfo__HasMethod(decl,"Dictionary") || IsTemplate(*decl))
   {
      finalString << "   static void " << mappedname.c_str() << "_Dictionary();\n"
                  << "   static void " << mappedname.c_str() << "_TClassManip(TClass*);\n";


   }

   if (HasIOConstructor(decl, args, ctorTypes, interp)) {
      finalString << "   static void *new_" << mappedname.c_str() << "(void *p = 0);" << "\n";

      if (args.size()==0 && NeedDestructor(decl))
      {
         finalString << "   static void *newArray_";
         finalString << mappedname.c_str();
         finalString << "(Long_t size, void *p);";
         finalString << "\n";
      }
   }

   if (NeedDestructor(decl)) {
      finalString << "   static void delete_" << mappedname.c_str() << "(void *p);" << "\n" << "   static void deleteArray_" << mappedname.c_str() << "(void *p);" << "\n" << "   static void destruct_" << mappedname.c_str() << "(void *p);" << "\n";
   }
   if (HasDirectoryAutoAdd(decl, interp)) {
      finalString << "   static void directoryAutoAdd_" << mappedname.c_str() << "(void *obj, TDirectory *dir);" << "\n";
   }
   if (HasCustomStreamerMemberFunction(cl, decl, interp, normCtxt)) {
      finalString << "   static void streamer_" << mappedname.c_str() << "(TBuffer &buf, void *obj);" << "\n";
   }
   if (HasNewMerge(decl, interp) || HasOldMerge(decl, interp)) {
      finalString << "   static Long64_t merge_" << mappedname.c_str() << "(void *obj, TCollection *coll,TFileMergeInfo *info);" << "\n";
   }
   if (HasResetAfterMerge(decl, interp)) {
      finalString << "   static void reset_" << mappedname.c_str() << "(void *obj, TFileMergeInfo *info);" << "\n";
   }

   //--------------------------------------------------------------------------
   // Check if we have any schema evolution rules for this class
   //--------------------------------------------------------------------------
   std::string declName;
   ROOT::TMetaUtils::GetQualifiedName(declName,*decl);
   ROOT::SchemaRuleClassMap_t::iterator rulesIt1 = ROOT::gReadRules.find( declName.c_str() );
   ROOT::SchemaRuleClassMap_t::iterator rulesIt2 = ROOT::gReadRawRules.find( declName.c_str() );

   ROOT::MembersTypeMap_t nameTypeMap;
   CreateNameTypeMap( *decl, nameTypeMap ); // here types for schema evo are written

   //--------------------------------------------------------------------------
   // Process the read rules
   //--------------------------------------------------------------------------
   if( rulesIt1 != ROOT::gReadRules.end() ) {
      int i = 0;
      finalString << "\n" << "   // Schema evolution read functions" << "\n";
      std::list<ROOT::SchemaRuleMap_t>::iterator rIt = rulesIt1->second.begin();
      while( rIt != rulesIt1->second.end() ) {

         //--------------------------------------------------------------------
         // Check if the rules refer to valid data members
         //--------------------------------------------------------------------
         if( !HasValidDataMembers( *rIt, nameTypeMap ) ) {
            rIt = rulesIt1->second.erase(rIt);
            continue;
         }

         //---------------------------------------------------------------------
         // Write the conversion function if necassary
         //---------------------------------------------------------------------
         if( rIt->find( "code" ) != rIt->end() ) {
            WriteReadRuleFunc( *rIt, i++, mappedname, nameTypeMap, finalString );
         }
         ++rIt;
      }
   }




   //--------------------------------------------------------------------------
   // Process the read raw rules
   //--------------------------------------------------------------------------
   if( rulesIt2 != ROOT::gReadRawRules.end() ) {
      int i = 0;
      finalString << "\n << ";   // Schema evolution read raw functions << "\n";
      std::list<ROOT::SchemaRuleMap_t>::iterator rIt = rulesIt2->second.begin();
      while( rIt != rulesIt2->second.end() ) {

         //--------------------------------------------------------------------
         // Check if the rules refer to valid data members
         //--------------------------------------------------------------------
         if( !HasValidDataMembers( *rIt, nameTypeMap ) ) {
            rIt = rulesIt2->second.erase(rIt);
            continue;
         }

         //---------------------------------------------------------------------
         // Write the conversion function
         //---------------------------------------------------------------------
         if( rIt->find( "code" ) == rIt->end() )
            continue;

         WriteReadRawRuleFunc( *rIt, i++, mappedname, nameTypeMap, finalString );
         ++rIt;
      }
   }

   finalString << "\n" << "   // Function generating the singleton type initializer" << "\n";

   finalString << "   static TGenericClassInfo *GenerateInitInstanceLocal(const " << csymbol.c_str() << "*)" << "\n" << "   {" << "\n";



   finalString << "      " << csymbol.c_str() << " *ptr = 0;" << "\n";

   //fprintf(fp, "      static ::ROOT::ClassInfo< %s > \n",classname.c_str());
   if (ClassInfo__HasMethod(decl,"IsA") ) {
      finalString << "      static ::TVirtualIsAProxy* isa_proxy = new ::TInstrumentedIsAProxy< "  << csymbol.c_str() << " >(0);" << "\n";
   }
   else {
      finalString << "      static ::TVirtualIsAProxy* isa_proxy = new ::TIsAProxy(typeid(" << csymbol.c_str() << "),0);" << "\n";
   }
   finalString << "      static ::ROOT::TGenericClassInfo " << "\n" << "         instance(\"" << classname.c_str() << "\", ";

   if (ClassInfo__HasMethod(decl,"Class_Version")) {
      finalString << csymbol.c_str() << "::Class_Version(), ";
   } else if (bset) {
      finalString << "2, "; // bitset 'version number'
   } else if (stl) {
      finalString << "-2, "; // "::TStreamerInfo::Class_Version(), ";
   } else if( cl.HasClassVersion() ) {
      finalString << cl.RequestedVersionNumber() << ", ";
   } else { // if (cl_input.RequestStreamerInfo()) {

      // Need to find out if the operator>> is actually defined for this class.
      static const char *versionFunc = "GetClassVersion";
      //      int ncha = strlen(classname.c_str())+strlen(versionFunc)+5;
      //      char *funcname= new char[ncha];
      //      snprintf(funcname,ncha,"%s<%s >",versionFunc,classname.c_str());
      std::string proto = classname + "*";
      const clang::Decl* ctxt = llvm::dyn_cast<clang::Decl>((*cl).getDeclContext());
      const clang::FunctionDecl *methodinfo = ROOT::TMetaUtils::GetFuncWithProto(ctxt, versionFunc, proto.c_str(), interp);
      //      delete [] funcname;

      if (methodinfo &&
          ROOT::TMetaUtils::GetFileName(methodinfo, interp).find("Rtypes.h") == llvm::StringRef::npos) {

         // GetClassVersion was defined in the header file.
         //fprintf(fp, "GetClassVersion((%s *)0x0), ",classname.c_str());
         finalString << "GetClassVersion< ";
         finalString << classname.c_str();
         finalString << " >(), ";
      }
      //static char temporary[1024];
      //sprintf(temporary,"GetClassVersion<%s>( (%s *) 0x0 )",classname.c_str(),classname.c_str());
      //fprintf(stderr,"DEBUG: %s has value %d\n",classname.c_str(),(int)G__int(G__calc(temporary)));
   }

   std::string filename = ROOT::TMetaUtils::GetFileName(cl, interp);
   if (filename.length() > 0) {
      for (unsigned int i=0; i<filename.length(); i++) {
         if (filename[i]=='\\') filename[i]='/';
      }
   }
   finalString << "\"" << filename << "\", " << ROOT::TMetaUtils::GetLineNumber(cl) << "," << "\n" << "                  typeid(" << csymbol.c_str() << "), DefineBehavior(ptr, ptr)," << "\n" << "                  ";

   if (ClassInfo__HasMethod(decl,"Dictionary") && !IsTemplate(*decl)) {
      finalString << "&" << csymbol.c_str() << "::Dictionary, ";
   } else {
      finalString << "&" << mappedname.c_str() << "_Dictionary, ";
   }

   enum {
      TClassTable__kHasCustomStreamerMember = 0x10 // See TClassTable.h
   };

   Int_t rootflag = cl.RootFlag();
   if (HasCustomStreamerMemberFunction(cl, decl, interp, normCtxt)) {
      rootflag = rootflag | TClassTable__kHasCustomStreamerMember;
   }
   finalString << "isa_proxy, " << rootflag << "," << "\n" << "                  sizeof(" << csymbol.c_str() << ") );" << "\n";
   if (HasIOConstructor(decl, args, ctorTypes, interp)) {
      finalString << "      instance.SetNew(&new_" << mappedname.c_str() << ");" << "\n";
      if (args.size()==0 && NeedDestructor(decl))
         finalString << "      instance.SetNewArray(&newArray_" << mappedname.c_str() << ");" << "\n";
   }
   if (NeedDestructor(decl)) {
      finalString << "      instance.SetDelete(&delete_" << mappedname.c_str() << ");" << "\n" << "      instance.SetDeleteArray(&deleteArray_" << mappedname.c_str() << ");" << "\n" << "      instance.SetDestructor(&destruct_" << mappedname.c_str() << ");" << "\n";
   }
   if (HasDirectoryAutoAdd(decl, interp)) {
      finalString << "      instance.SetDirectoryAutoAdd(&directoryAutoAdd_" << mappedname.c_str() << ");" << "\n";
   }
   if (HasCustomStreamerMemberFunction(cl, decl, interp, normCtxt)) {
      // We have a custom member function streamer or an older (not StreamerInfo based) automatic streamer.
      finalString << "      instance.SetStreamerFunc(&streamer_" << mappedname.c_str() << ");" << "\n";
   }
   if (HasNewMerge(decl, interp) || HasOldMerge(decl, interp)) {
      finalString << "      instance.SetMerge(&merge_" << mappedname.c_str() << ");" << "\n";
   }
   if (HasResetAfterMerge(decl, interp)) {
      finalString << "      instance.SetResetAfterMerge(&reset_" << mappedname.c_str() << ");" << "\n";
   }
   if (bset) {
      finalString << "      instance.AdoptCollectionProxyInfo(TCollectionProxyInfo::Generate(TCollectionProxyInfo::" << "Pushback" << "<TStdBitsetHelper< " << classname.c_str() << " > >()));" << "\n";

      needCollectionProxy = true;
   } else if (stl != 0 && ((stl>0 && stl<8) || (stl<0 && stl>-8)) )  {
      int idx = classname.find("<");
      int stlType = (idx!=(int)std::string::npos) ? TClassEdit::STLKind(classname.substr(0,idx).c_str()) : 0;
      const char* methodTCP=0;
      switch(stlType)  {
         case ROOT::kSTLvector:
         case ROOT::kSTLlist:
         case ROOT::kSTLdeque:
            methodTCP="Pushback";
            break;
         case ROOT::kSTLmap:
         case ROOT::kSTLmultimap:
            methodTCP="MapInsert";
            break;
         case ROOT::kSTLset:
         case ROOT::kSTLmultiset:
            methodTCP="Insert";
            break;
      }
      finalString << "      instance.AdoptCollectionProxyInfo(TCollectionProxyInfo::Generate(TCollectionProxyInfo::" << methodTCP << "< " << classname.c_str() << " >()));" << "\n";

      needCollectionProxy = true;
   }

   //---------------------------------------------------------------------------
   // Pass the schema evolution rules to TGenericClassInfo
   //---------------------------------------------------------------------------
   if( (rulesIt1 != ROOT::gReadRules.end() && rulesIt1->second.size()>0) || (rulesIt2 != ROOT::gReadRawRules.end()  && rulesIt2->second.size()>0) ) {
      finalString << "\n" << "      ROOT::TSchemaHelper* rule;" << "\n";
   }

   if( rulesIt1 != ROOT::gReadRules.end() ) {
      finalString << "\n" << "      // the io read rules" << "\n" << "      std::vector<ROOT::TSchemaHelper> readrules(" << rulesIt1->second.size() << ");" << "\n";
      ROOT::WriteSchemaList( rulesIt1->second, "readrules", finalString );
      finalString << "      instance.SetReadRules( readrules );" << "\n";
   }

   if( rulesIt2 != ROOT::gReadRawRules.end() ) {
      finalString << "\n" << "      // the io read raw rules" << "\n" << "      std::vector<ROOT::TSchemaHelper> readrawrules(" << rulesIt2->second.size() << ");" << "\n";
      ROOT::WriteSchemaList( rulesIt2->second, "readrawrules", finalString );
      finalString << "      instance.SetReadRawRules( readrawrules );" << "\n";
   }

   finalString << "      return &instance;" << "\n" << "   }" << "\n";

   if (!isStdNotString && !ROOT::TMetaUtils::hasOpaqueTypedef(cl, interp, normCtxt)) {
      // The GenerateInitInstance for STL are not unique and should not be externally accessible
      finalString << "   TGenericClassInfo *GenerateInitInstance(const " << csymbol.c_str() << "*)" << "\n" << "   {\n      return GenerateInitInstanceLocal((" << csymbol.c_str() << "*)0);\n   }" << "\n";
   }

   finalString << "   // Static variable to force the class initialization" << "\n";
   // must be one long line otherwise UseDummy does not work


   finalString << "   static ::ROOT::TGenericClassInfo *_R__UNIQUE_(Init) = GenerateInitInstanceLocal((const " << csymbol.c_str() << "*)0x0); R__UseDummy(_R__UNIQUE_(Init));" << "\n";

   if (!ClassInfo__HasMethod(decl,"Dictionary") || IsTemplate(*decl)) {
      const char* cSymbolStr = csymbol.c_str();
      finalString <<  "\n" << "   // Dictionary for non-ClassDef classes" << "\n"
                  << "   static void " << mappedname.c_str() << "_Dictionary() {\n"
                  << "      TClass* theClass ="
                  << "::ROOT::GenerateInitInstanceLocal((const " << cSymbolStr << "*)0x0)->GetClass();\n"
                  << "      " << mappedname << "_TClassManip(theClass);\n";

      finalString << "   }\n\n";

      // Now manipulate tclass in order to percolate the properties expressed as
      // annotations of the decls.
      std::string manipString;
      std::size_t substrFound;
      std::string attribute_s;
      std::string attrName, attrValue;
      // Class properties
      bool attrMapExtracted = false;
      if (decl->hasAttrs()){
         // Loop on the attributes
         for (clang::Decl::attr_iterator attrIt = decl->attr_begin();
              attrIt!=decl->attr_end();++attrIt){
            if ( 0!=ROOT::TMetaUtils::extractAttrString(*attrIt,attribute_s)){
               continue;
            }
            if (0!=ROOT::TMetaUtils::extractPropertyNameValFromString(attribute_s, attrName, attrValue)){
               continue;
            }
            if (attrName == "name" ||
                attrName == "pattern" ||
                attrName == "rootmap") continue;
            // A general property
            // 1) We need to create the property map (in the gen code)
            // 2) we need to take out the map (in the gen code)
            // 3) We need to bookkep the fact that the map is created and out (in this source)
            // 4) We fill the map (in the gen code)
            if (!attrMapExtracted){
               manipString+="      theClass->CreateAttributeMap();\n";
               manipString+="      TDictAttributeMap* attrMap( theClass->GetAttributeMap() );\n";
               attrMapExtracted=true;
            }

            // Format property as String
            attrValue = "\""+attrValue+"\"";

            manipString+="      attrMap->AddProperty(\""+attrName +"\","+attrValue+");\n";
         }
      } // end of class has properties

      // Member properties
      // NOTE: Only transiency propagated

      // Loop on declarations inside the class, including data members
      bool tDataMemberPtrGot=false;
      for(clang::CXXRecordDecl::decl_iterator internalDeclIt = decl->decls_begin();
          internalDeclIt != decl->decls_end(); ++internalDeclIt){
         if (!(!(*internalDeclIt)->isImplicit()
            && (clang::isa<clang::FieldDecl>(*internalDeclIt) ||
                clang::isa<clang::VarDecl>(*internalDeclIt)))) continue; // Check if it's a var or a field
         // Now let's check the attributes of the var/field
         if (!internalDeclIt->hasAttrs()) continue;
         for (clang::Decl::attr_iterator attrIt = internalDeclIt->attr_begin();
              attrIt!=internalDeclIt->attr_end();++attrIt){
            // Convert the attribute to AnnotateAttr if possible
            clang::AnnotateAttr* annAttr = clang::dyn_cast<clang::AnnotateAttr>(*attrIt);
            if (!annAttr) continue;
            // Let's see if it's a transient attribute
            attribute_s = annAttr->getAnnotation();
            std::string attributeNoSpaces_s (attribute_s);
            std::remove(attributeNoSpaces_s.begin(), attributeNoSpaces_s.end(), ' ');
            // 1) Let's see if it's a //!
            substrFound = attributeNoSpaces_s.find("//!");
            if (substrFound != 0) continue;
            // 2) Add the modification lines to the manipString.
            //    Get the TDataMamber and then set it Transient
            clang::NamedDecl* namedInternalDecl = clang::dyn_cast<clang::NamedDecl> (*internalDeclIt);
            if (!namedInternalDecl) {
               TMetaUtils::Error(0,"Cannot convert field declaration to clang::NamedDecl");
               continue;
            };
            std::string memberName(namedInternalDecl->getName());
            if (!tDataMemberPtrGot){
               manipString+="      TDataMember* ";
               tDataMemberPtrGot = true;
            }
            manipString+="      theMember = theClass->GetDataMember(\""+memberName+"\");\n"
                         "      theMember->ResetBit(BIT(2));\n"; //FIXME: Make it less cryptic


         } // End loop on attributes
      } // End loop on internal declarations


      finalString << "   static void " << mappedname << "_TClassManip(TClass* " << (manipString.empty() ? "":"theClass") << "){\n"
                  << manipString
                  << "   }\n\n";
   } // End of !ClassInfo__HasMethod(decl,"Dictionary") || IsTemplate(*decl))

   finalString << "} // end of namespace ROOT" << "\n" << "\n";
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::GetNameWithinNamespace(std::string &fullname,
                                                 std::string &clsname,
                                                 std::string &nsname,
                                                 const clang::CXXRecordDecl *cl)
{
   // Return true if one of the class' enclosing scope is a namespace and
   // set fullname to the fully qualified name,
   // clsname to the name within a namespace
   // and nsname to the namespace fully qualified name.

   fullname.clear();
   nsname.clear();

   ROOT::TMetaUtils::GetQualifiedName(fullname,*cl);
   clsname = fullname;

   const clang::NamedDecl *ctxt = llvm::dyn_cast<clang::NamedDecl>(cl->getEnclosingNamespaceContext());
   if (ctxt && ctxt!=cl) {
      const clang::NamespaceDecl *nsdecl = llvm::dyn_cast<clang::NamespaceDecl>(ctxt);
      if (nsdecl == 0 || !nsdecl->isAnonymousNamespace()) {
         ROOT::TMetaUtils::GetQualifiedName(nsname,*nsdecl);
         clsname.erase (0, nsname.size() + 2);
         return true;
      }
   }
   return false;
}

//______________________________________________________________________________
const clang::DeclContext *GetEnclosingSpace(const clang::RecordDecl &cl)
{
   const clang::DeclContext *ctxt = cl.getDeclContext();
   while(ctxt && !ctxt->isNamespace()) {
      ctxt = ctxt->getParent();
   }
   return ctxt;
}

//______________________________________________________________________________
int ROOT::TMetaUtils::WriteNamespaceHeader(std::ostream &out, const clang::RecordDecl *cl)
{
   return ::WriteNamespaceHeader(out, GetEnclosingSpace(*cl));
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::NeedTemplateKeyword(const clang::CXXRecordDecl *cl)
{
   clang::TemplateSpecializationKind kind = cl->getTemplateSpecializationKind();
   if (kind == clang::TSK_Undeclared ) {
      // Note a template;
      return false;
   } else if (kind == clang::TSK_ExplicitSpecialization) {
      // This is a specialized templated class
      return false;
   } else {
      // This is an automatically or explicitly instantiated templated class.
      return true;
   }
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::HasCustomOperatorNewPlacement(const char *which, const clang::RecordDecl &cl, const cling::Interpreter &interp)
{
   // return true if we can find a custom operator new with placement

   const char *name = which;
   const char *proto = "size_t";
   const char *protoPlacement = "size_t,void*";

   // First search in the enclosing namespaces
   const clang::FunctionDecl *operatornew = ROOT::TMetaUtils::GetFuncWithProto(llvm::dyn_cast<clang::Decl>(cl.getDeclContext()), name, proto, interp);
   const clang::FunctionDecl *operatornewPlacement = ROOT::TMetaUtils::GetFuncWithProto(llvm::dyn_cast<clang::Decl>(cl.getDeclContext()), name, protoPlacement, interp);

   const clang::DeclContext *ctxtnew = 0;
   const clang::DeclContext *ctxtnewPlacement = 0;

   if (operatornew) {
      ctxtnew = operatornew->getParent();
   }
   if (operatornewPlacement) {
      ctxtnewPlacement = operatornewPlacement->getParent();
   }

   // Then in the class and base classes
   operatornew = ROOT::TMetaUtils::GetFuncWithProto(&cl, name, proto, interp);
   operatornewPlacement = ROOT::TMetaUtils::GetFuncWithProto(&cl, name, protoPlacement, interp);

   if (operatornew) {
      ctxtnew = operatornew->getParent();
   }
   if (operatornewPlacement) {
      ctxtnewPlacement = operatornewPlacement->getParent();
   }

   if (ctxtnewPlacement == 0) {
      return false;
   }
   if (ctxtnew == 0) {
      // Only a new with placement, no hiding
      return true;
   }
   // Both are non zero
   if (ctxtnew == ctxtnewPlacement) {
      // Same declaration ctxt, no hiding
      return true;
   }
   const clang::CXXRecordDecl* clnew = llvm::dyn_cast<clang::CXXRecordDecl>(ctxtnew);
   const clang::CXXRecordDecl* clnewPlacement = llvm::dyn_cast<clang::CXXRecordDecl>(ctxtnewPlacement);
   if (clnew == 0 && clnewPlacement == 0) {
      // They are both in different namespaces, I am not sure of the rules.
      // we probably ought to find which one is closest ... for now bail
      // (because rootcling was also bailing on that).
      return true;
   }
   if (clnew != 0 && clnewPlacement == 0) {
      // operator new is class method hiding the outer scope operator new with placement.
      return false;
   }
   if (clnew == 0 && clnewPlacement != 0) {
      // operator new is a not class method and can not hide new with placement which is a method
      return true;
   }
   // Both are class methods
   if (clnew->isDerivedFrom(clnewPlacement)) {
      // operator new is in a more derived part of the hierarchy, it is hiding operator new with placement.
      return false;
   }
   // operator new with placement is in a more derived part of the hierarchy, it can't be hidden by operator new.
   return true;
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::HasCustomOperatorNewPlacement(const clang::RecordDecl &cl, const cling::Interpreter &interp)
{
   // return true if we can find a custom operator new with placement

   return HasCustomOperatorNewPlacement("operator new",cl, interp);
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::HasCustomOperatorNewArrayPlacement(const clang::RecordDecl &cl, const cling::Interpreter &interp)
{
   // return true if we can find a custom operator new with placement

   return HasCustomOperatorNewPlacement("operator new[]",cl, interp);
}

//______________________________________________________________________________
void ROOT::TMetaUtils::WriteAuxFunctions(std::ostream& finalString,
                                         const AnnotatedRecordDecl &cl,
                                         const clang::CXXRecordDecl *decl,
                                         const cling::Interpreter &interp,
                                         const RConstructorTypes& ctorTypes,
                                         const TNormalizedCtxt &normCtxt)
{
   // std::string NormalizedName;
   // GetNormalizedName(NormalizedName, decl->getASTContext().getTypeDeclType(decl), interp, normCtxt);

   std::string classname = TClassEdit::GetLong64_Name(cl.GetNormalizedName());

   std::string mappedname;
   ROOT::TMetaUtils::GetCppName(mappedname,classname.c_str());

   // Write the functions that are need for the TGenericClassInfo.
   // This includes
   //    IsA
   //    operator new
   //    operator new[]
   //    operator delete
   //    operator delete[]

   ROOT::TMetaUtils::GetCppName(mappedname,classname.c_str());

   if ( ! TClassEdit::IsStdClass( classname.c_str() ) ) {

      // Prefix the full class name with '::' except for the STL
      // containers and std::string.  This is to request the
      // real class instead of the class in the namespace ROOT::Shadow
      classname.insert(0,"::");
   }

   finalString << "namespace ROOT {" << "\n";

   std::string args;
   if (HasIOConstructor(decl, args, ctorTypes, interp)) {
      // write the constructor wrapper only for concrete classes
      finalString << "   // Wrappers around operator new" << "\n";
      finalString << "   static void *new_" << mappedname.c_str() << "(void *p) {" << "\n" << "      return  p ? ";
      if (HasCustomOperatorNewPlacement(*decl, interp)) {
         finalString << "new(p) ";
         finalString << classname.c_str();
         finalString << args;
         finalString << " : ";
      } else {
         finalString << "::new((::ROOT::TOperatorNewHelper*)p) ";
         finalString << classname.c_str();
         finalString << args;
         finalString << " : ";
      }
      finalString << "new " << classname.c_str() << args << ";" << "\n";
      finalString << "   }" << "\n";

      if (args.size()==0 && NeedDestructor(decl)) {
         // Can not can newArray if the destructor is not public.
         finalString << "   static void *newArray_";
         finalString << mappedname.c_str();
         finalString << "(Long_t nElements, void *p) {";
         finalString << "\n";
         finalString << "      return p ? ";
         if (HasCustomOperatorNewArrayPlacement(*decl, interp)) {
            finalString << "new(p) ";
            finalString << classname.c_str();
            finalString << "[nElements] : ";
         } else {
            finalString << "::new((::ROOT::TOperatorNewHelper*)p) ";
            finalString << classname.c_str();
            finalString << "[nElements] : ";
         }
         finalString << "new ";
         finalString << classname.c_str();
         finalString << "[nElements];";
         finalString << "\n";
         finalString << "   }";
         finalString << "\n";
      }
   }

   if (NeedDestructor(decl)) {
      finalString << "   // Wrapper around operator delete" << "\n" << "   static void delete_" << mappedname.c_str() << "(void *p) {" << "\n" << "      delete ((" << classname.c_str() << "*)p);" << "\n" << "   }" << "\n" << "   static void deleteArray_" << mappedname.c_str() << "(void *p) {" << "\n" << "      delete [] ((" << classname.c_str() << "*)p);" << "\n" << "   }" << "\n" << "   static void destruct_" << mappedname.c_str() << "(void *p) {" << "\n" << "      typedef " << classname.c_str() << " current_t;" << "\n" << "      ((current_t*)p)->~current_t();" << "\n" << "   }" << "\n";
   }

   if (HasDirectoryAutoAdd(decl, interp)) {
       finalString << "   // Wrapper around the directory auto add." << "\n" << "   static void directoryAutoAdd_" << mappedname.c_str() << "(void *p, TDirectory *dir) {" << "\n" << "      ((" << classname.c_str() << "*)p)->DirectoryAutoAdd(dir);" << "\n" << "   }" << "\n";
   }

   if (HasCustomStreamerMemberFunction(cl, decl, interp, normCtxt)) {
      finalString << "   // Wrapper around a custom streamer member function." << "\n" << "   static void streamer_" << mappedname.c_str() << "(TBuffer &buf, void *obj) {" << "\n" << "      ((" << classname.c_str() << "*)obj)->" << classname.c_str() << "::Streamer(buf);" << "\n" << "   }" << "\n";
   }

   if (HasNewMerge(decl, interp)) {
      finalString << "   // Wrapper around the merge function." << "\n" << "   static Long64_t merge_" << mappedname.c_str() << "(void *obj,TCollection *coll,TFileMergeInfo *info) {" << "\n" << "      return ((" << classname.c_str() << "*)obj)->Merge(coll,info);" << "\n" << "   }" << "\n";
   } else if (HasOldMerge(decl, interp)) {
      finalString << "   // Wrapper around the merge function." << "\n" << "   static Long64_t  merge_" << mappedname.c_str() << "(void *obj,TCollection *coll,TFileMergeInfo *) {" << "\n" << "      return ((" << classname.c_str() << "*)obj)->Merge(coll);" << "\n" << "   }" << "\n";
   }

   if (HasResetAfterMerge(decl, interp)) {
      finalString << "   // Wrapper around the Reset function." << "\n" << "   static void reset_" << mappedname.c_str() << "(void *obj,TFileMergeInfo *info) {" << "\n" << "      ((" << classname.c_str() << "*)obj)->ResetAfterMerge(info);" << "\n" << "   }" << "\n";
   }
   finalString << "} // end of namespace ROOT for class " << classname.c_str() << "\n" << "\n";
}

//______________________________________________________________________________
void ROOT::TMetaUtils::WritePointersSTL(const AnnotatedRecordDecl &cl,
                                        const cling::Interpreter &interp,
                                        const TNormalizedCtxt &normCtxt)
{
   // Write interface function for STL members

   std::string a;
   std::string clName;
   TMetaUtils::GetCppName(clName, ROOT::TMetaUtils::GetFileName(cl.GetRecordDecl(), interp).str().c_str());
   int version = ROOT::TMetaUtils::GetClassVersion(cl.GetRecordDecl());
   if (version == 0) return;
   if (version < 0 && !(cl.RequestStreamerInfo()) ) return;


   const clang::CXXRecordDecl *clxx = llvm::dyn_cast<clang::CXXRecordDecl>(cl.GetRecordDecl());
   if (clxx == 0) return;

   // We also need to look at the base classes.
   for(clang::CXXRecordDecl::base_class_const_iterator iter = clxx->bases_begin(), end = clxx->bases_end();
       iter != end;
       ++iter)
   {
      int k = ROOT::TMetaUtils::IsSTLContainer(*iter);
      if (k!=0) {
         RStl::Instance().GenerateTClassFor( iter->getType(), interp, normCtxt);
      }
   }

   // Loop over the non static data member.
   for(clang::RecordDecl::field_iterator field_iter = clxx->field_begin(), end = clxx->field_end();
       field_iter != end;
       ++field_iter)
   {
      std::string mTypename;
      ROOT::TMetaUtils::GetQualifiedName(mTypename, field_iter->getType(), *clxx);

      //member is a string
      {
         const char*shortTypeName = ROOT::TMetaUtils::ShortTypeName(mTypename.c_str());
         if (!strcmp(shortTypeName, "string")) {
            continue;
         }
      }

      if (!ROOT::TMetaUtils::IsStreamableObject(**field_iter)) continue;

      int k = ROOT::TMetaUtils::IsSTLContainer( **field_iter );
      if (k!=0) {
         //          fprintf(stderr,"Add %s which is also",m.Type()->Name());
         //          fprintf(stderr," %s\n",R__TrueName(**field_iter) );
         clang::QualType utype(ROOT::TMetaUtils::GetUnderlyingType(field_iter->getType()),0);
         RStl::Instance().GenerateTClassFor(utype, interp, normCtxt);
      }
   }
}

//______________________________________________________________________________
std::string ROOT::TMetaUtils::TrueName(const clang::FieldDecl &m)
{
   // TrueName strips the typedefs and array dimensions.

   const clang::Type *rawtype = m.getType()->getCanonicalTypeInternal().getTypePtr();
   if (rawtype->isArrayType()) {
      rawtype = rawtype->getBaseElementTypeUnsafe ();
   }

   std::string result;
   ROOT::TMetaUtils::GetQualifiedName(result, clang::QualType(rawtype,0), m);
   return result;
}

//______________________________________________________________________________
int ROOT::TMetaUtils::GetClassVersion(const clang::RecordDecl *cl)
{
   // Return the version number of the class or -1
   // if the function Class_Version does not exist.

   if (!ROOT::TMetaUtils::ClassInfo__HasMethod(cl,"Class_Version")) return -1;

   const clang::CXXRecordDecl* CRD = llvm::dyn_cast<clang::CXXRecordDecl>(cl);
   if (!CRD) {
      // Must be an enum or namespace.
      // FIXME: Make it work for a namespace!
      return false;
   }
   // Class_Version is know to be inline and we constrol (via the ClassDef macros)
   // it's structure, so this is apriori fine, but we could consider replacing it
   // with the slower but simpler:
   //   gInterp->evaluate( classname + "::Class_Version()", &Value);
   std::string given_name("Class_Version");
   for (
        clang::CXXRecordDecl::method_iterator M = CRD->method_begin(),
        MEnd = CRD->method_end();
        M != MEnd;
        ++M
        ) {
      if (M->getNameAsString() == given_name) {
         clang::CompoundStmt *func = 0;
         if (M->getBody()) {
            func = llvm::dyn_cast<clang::CompoundStmt>(M->getBody());
         } else {
            const clang::FunctionDecl *inst = M->getInstantiatedFromMemberFunction();
            if (inst && inst->getBody()) {
               func = llvm::dyn_cast<clang::CompoundStmt>(inst->getBody());
            } else {
               std::string qualName;
               ROOT::TMetaUtils::GetQualifiedName(qualName,*cl);
               ROOT::TMetaUtils::Error("GetClassVersion","Could not find the body for %s::ClassVersion!\n",qualName.c_str());
            }
         }
         if (func && !func->body_empty()) {
            clang::ReturnStmt *ret = llvm::dyn_cast<clang::ReturnStmt>(*func->body_begin());
            if (ret) {
               clang::IntegerLiteral *val;
               clang::ImplicitCastExpr *cast = llvm::dyn_cast<clang::ImplicitCastExpr>( ret->getRetValue() );
               if (cast) {
                  val = llvm::dyn_cast<clang::IntegerLiteral>( cast->getSubExprAsWritten() );
               } else {
                  val = llvm::dyn_cast<clang::IntegerLiteral>( ret->getRetValue() );
               }
               if (val) {
                  return (int)val->getValue().getLimitedValue(~0);
               }
            }
         }
         return 0;
      }
   }
   return 0;
}

//______________________________________________________________________________
int ROOT::TMetaUtils::IsSTLContainer(const ROOT::TMetaUtils::AnnotatedRecordDecl &annotated)
{
   // Is this an STL container.

   return TMetaUtils::IsSTLCont(*annotated.GetRecordDecl());
}

//______________________________________________________________________________
ROOT::ESTLType ROOT::TMetaUtils::IsSTLContainer(const clang::FieldDecl &m)
{
   // Is this an STL container?

   clang::QualType type = m.getType();
   clang::RecordDecl *decl = ROOT::TMetaUtils::GetUnderlyingRecordDecl(type);

   if (decl) return TMetaUtils::IsSTLCont(*decl);
   else return ROOT::kNotSTL;
}

//______________________________________________________________________________
int ROOT::TMetaUtils::IsSTLContainer(const clang::CXXBaseSpecifier &base)
{
   // Is this an STL container?

   clang::QualType type = base.getType();
   clang::RecordDecl *decl = ROOT::TMetaUtils::GetUnderlyingRecordDecl(type);

   if (decl) return TMetaUtils::IsSTLCont(*decl);
   else return ROOT::kNotSTL;
}

//______________________________________________________________________________
const char *ROOT::TMetaUtils::ShortTypeName(const char *typeDesc)
{
   // Return the absolute type of typeDesc.
   // E.g.: typeDesc = "class TNamed**", returns "TNamed".
   // we remove * and const keywords. (we do not want to remove & ).
   // You need to use the result immediately before it is being overwritten.

   static char t[4096];
   static const char* constwd = "const ";
   static const char* constwdend = "const";

   const char *s;
   char *p=t;
   int lev=0;
   for (s=typeDesc;*s;s++) {
      if (*s=='<') lev++;
      if (*s=='>') lev--;
      if (lev==0 && *s=='*') continue;
      if (lev==0 && (strncmp(constwd,s,strlen(constwd))==0
                     ||strcmp(constwdend,s)==0 ) ) {
         s+=strlen(constwd)-1; // -1 because the loop adds 1
         continue;
      }
      if (lev==0 && *s==' ' && *(s+1)!='*') { p = t; continue;}
      if (p - t > (long)sizeof(t)) {
         printf("ERROR (rootcling): type name too long for StortTypeName: %s\n",
                typeDesc);
         p[0] = 0;
         return t;
      }
      *p++ = *s;
   }
   p[0]=0;

   return t;
}

bool ROOT::TMetaUtils::IsStreamableObject(const clang::FieldDecl &m)
{
   const char *comment = ROOT::TMetaUtils::GetComment( m ).data();

   // Transient
   if (comment[0] == '!') return false;

   clang::QualType type = m.getType();

   if (type->isReferenceType()) {
      // Reference can not be streamed.
      return false;
   }

   std::string mTypeName = type.getAsString(m.getASTContext().getPrintingPolicy());
   if (!strcmp(mTypeName.c_str(), "string") || !strcmp(mTypeName.c_str(), "string*")) {
      return true;
   }
   if (!strcmp(mTypeName.c_str(), "std::string") || !strcmp(mTypeName.c_str(), "std::string*")) {
      return true;
   }

   if (ROOT::TMetaUtils::IsSTLContainer(m)) {
      return true;
   }

   const clang::Type *rawtype = type.getTypePtr()->getBaseElementTypeUnsafe ();

   if (rawtype->isPointerType()) {
      //Get to the 'raw' type.
      clang::QualType pointee;
      while ( (pointee = rawtype->getPointeeType()) , pointee.getTypePtrOrNull() && pointee.getTypePtr() != rawtype)
      {
        rawtype = pointee.getTypePtr();
      }
   }

   if (rawtype->isFundamentalType() || rawtype->isEnumeralType()) {
      // not an ojbect.
      return false;
   }

   const clang::CXXRecordDecl *cxxdecl = rawtype->getAsCXXRecordDecl();
   if (cxxdecl && ROOT::TMetaUtils::ClassInfo__HasMethod(cxxdecl,"Streamer")) {
      if (!(ROOT::TMetaUtils::ClassInfo__HasMethod(cxxdecl,"Class_Version"))) return true;
      int version = ROOT::TMetaUtils::GetClassVersion(cxxdecl);
      if (version > 0) return true;
   }
   return false;
}

//______________________________________________________________________________
std::string ROOT::TMetaUtils::ShortTypeName(const clang::FieldDecl &m)
{
   // Return the absolute type of typeDesc.
   // E.g.: typeDesc = "class TNamed**", returns "TNamed".
   // we remove * and const keywords. (we do not want to remove & ).
   // You need to use the result immediately before it is being overwritten.

   const clang::Type *rawtype = m.getType().getTypePtr();

   //Get to the 'raw' type.
   clang::QualType pointee;
   while ( rawtype->isPointerType() && ((pointee = rawtype->getPointeeType()) , pointee.getTypePtrOrNull()) && pointee.getTypePtr() != rawtype)
   {
      rawtype = pointee.getTypePtr();
   }

   std::string result;
   ROOT::TMetaUtils::GetQualifiedName(result, clang::QualType(rawtype,0), m);
   return result;
}

//______________________________________________________________________________
clang::RecordDecl *ROOT::TMetaUtils::GetUnderlyingRecordDecl(clang::QualType type)
{
   const clang::Type *rawtype = ROOT::TMetaUtils::GetUnderlyingType(type);

   if (rawtype->isFundamentalType() || rawtype->isEnumeralType()) {
      // not an ojbect.
      return 0;
   }
   return rawtype->getAsCXXRecordDecl();
}

//______________________________________________________________________________
void ROOT::TMetaUtils::WriteClassCode(CallWriteStreamer_t WriteStreamerFunc,
                                      const AnnotatedRecordDecl &cl,
                                      const cling::Interpreter &interp,
                                      const TNormalizedCtxt &normCtxt,
                                      std::ostream& dictStream,
                                      const RConstructorTypes& ctorTypes,
                                      bool isGenreflex=false)
{
   // Generate the code of the class
   // If the requestor is genreflex, request the new streamer format

   const clang::CXXRecordDecl* decl = llvm::dyn_cast<clang::CXXRecordDecl>(cl.GetRecordDecl());

   if (!decl  || !decl->isCompleteDefinition()) {
      return;
   }

   std::string fullname;
   ROOT::TMetaUtils::GetQualifiedName(fullname,cl);
   if (TClassEdit::IsSTLCont(fullname.c_str()) ) {
     RStl::Instance().GenerateTClassFor(cl.GetNormalizedName(), llvm::dyn_cast<clang::CXXRecordDecl>(cl.GetRecordDecl()), interp, normCtxt);
     return;
   }

   if (ROOT::TMetaUtils::ClassInfo__HasMethod(cl,"Streamer")) {
      if (cl.RootFlag()) ROOT::TMetaUtils::WritePointersSTL(cl, interp, normCtxt); // In particular this detect if the class has a version number.
      if (!(cl.RequestNoStreamer())) {
         (*WriteStreamerFunc)(cl, interp, normCtxt, dictStream, isGenreflex || cl.RequestStreamerInfo());
      } else
         ROOT::TMetaUtils::Info(0, "Class %s: Do not generate Streamer() [*** custom streamer ***]\n",fullname.c_str());
   } else {
      ROOT::TMetaUtils::Info(0, "Class %s: Streamer() not declared\n", fullname.c_str());

      if (cl.RequestStreamerInfo()) ROOT::TMetaUtils::WritePointersSTL(cl, interp, normCtxt);
   }
   ROOT::TMetaUtils::WriteAuxFunctions(dictStream, cl, decl, interp, ctorTypes, normCtxt);
}

//______________________________________________________________________________
void ROOT::TMetaUtils::LevelPrint(bool prefix, int level, const char *location, const char *fmt, va_list ap)
{
   if (level < ROOT::TMetaUtils::gErrorIgnoreLevel)
      return;

   const char *type = 0;

   if (level >= ROOT::TMetaUtils::kInfo)
      type = "Info";
   if (level >= ROOT::TMetaUtils::kNote)
      type = "Note";
   if (level >= ROOT::TMetaUtils::kWarning)
      type = "Warning";
   if (level >= ROOT::TMetaUtils::kError)
      type = "Error";
   if (level >= ROOT::TMetaUtils::kSysError)
      type = "SysError";
   if (level >= ROOT::TMetaUtils::kFatal)
      type = "Fatal";

   if (!location || !location[0]) {
      if (prefix) fprintf(stderr, "%s: ", type);
      vfprintf(stderr, (const char*)va_(fmt), ap);
   } else {
      if (prefix) fprintf(stderr, "%s in <%s>: ", type, location);
      else fprintf(stderr, "In <%s>: ", location);
      vfprintf(stderr, (const char*)va_(fmt), ap);
   }

   fflush(stderr);
}

//______________________________________________________________________________
void ROOT::TMetaUtils::Error(const char *location, const char *va_(fmt), ...)
{
   // Use this function in case an error occured.

   va_list ap;
   va_start(ap,va_(fmt));
   LevelPrint(true, ROOT::TMetaUtils::kError, location, va_(fmt), ap);
   va_end(ap);
}

//______________________________________________________________________________
void ROOT::TMetaUtils::SysError(const char *location, const char *va_(fmt), ...)
{
   // Use this function in case a system (OS or GUI) related error occured.

   va_list ap;
   va_start(ap, va_(fmt));
   LevelPrint(true, ROOT::TMetaUtils::kSysError, location, va_(fmt), ap);
   va_end(ap);
}

//______________________________________________________________________________
void ROOT::TMetaUtils::Info(const char *location, const char *va_(fmt), ...)
{
   // Use this function for informational messages.

   va_list ap;
   va_start(ap,va_(fmt));
   LevelPrint(true, ROOT::TMetaUtils::kInfo, location, va_(fmt), ap);
   va_end(ap);
}

//______________________________________________________________________________
void ROOT::TMetaUtils::Warning(const char *location, const char *va_(fmt), ...)
{
   // Use this function in warning situations.

   va_list ap;
   va_start(ap,va_(fmt));
   LevelPrint(true, ROOT::TMetaUtils::kWarning, location, va_(fmt), ap);
   va_end(ap);
}

//______________________________________________________________________________
void ROOT::TMetaUtils::Fatal(const char *location, const char *va_(fmt), ...)
{
   // Use this function in case of a fatal error. It will abort the program.

   va_list ap;
   va_start(ap,va_(fmt));
   LevelPrint(true, ROOT::TMetaUtils::kFatal, location, va_(fmt), ap);
   va_end(ap);
}

//______________________________________________________________________________
clang::QualType ROOT::TMetaUtils::AddDefaultParameters(clang::QualType instanceType,
                                                       const cling::Interpreter &interpreter,
                                                       const ROOT::TMetaUtils::TNormalizedCtxt &normCtxt)
{
   // Add any unspecified template parameters to the class template instance,
   // mentioned anywhere in the type.
   //
   // Note: this does not strip any typedef but could be merged with cling::utils::Transform::GetPartiallyDesugaredType
   // if we can safely replace TClassEdit::IsStd with a test on the declaring scope
   // and if we can resolve the fact that the added parameter do not take into account possible use/dependences on Double32_t
   // and if we decide that adding the default is the right long term solution or not.
   // Whether it is or not depend on the I/O on whether the default template argument might change or not
   // and whether they (should) affect the on disk layout (for STL containers, we do know they do not).

   const clang::ASTContext& Ctx = interpreter.getCI()->getASTContext();

   // In case of name* we need to strip the pointer first, add the default and attach
   // the pointer once again.
   if (llvm::isa<clang::PointerType>(instanceType.getTypePtr())) {
      // Get the qualifiers.
      clang::Qualifiers quals = instanceType.getQualifiers();
      instanceType = AddDefaultParameters(instanceType->getPointeeType(), interpreter, normCtxt);
      instanceType = Ctx.getPointerType(instanceType);
      // Add back the qualifiers.
      instanceType = Ctx.getQualifiedType(instanceType, quals);
   }

   // In case of Int_t& we need to strip the pointer first, desugar and attach
   // the pointer once again.
   if (llvm::isa<clang::ReferenceType>(instanceType.getTypePtr())) {
      // Get the qualifiers.
      bool isLValueRefTy = llvm::isa<clang::LValueReferenceType>(instanceType.getTypePtr());
      clang::Qualifiers quals = instanceType.getQualifiers();
      instanceType = AddDefaultParameters(instanceType->getPointeeType(), interpreter, normCtxt);

      // Add the r- or l- value reference type back to the desugared one
      if (isLValueRefTy)
        instanceType = Ctx.getLValueReferenceType(instanceType);
      else
        instanceType = Ctx.getRValueReferenceType(instanceType);
      // Add back the qualifiers.
      instanceType = Ctx.getQualifiedType(instanceType, quals);
   }

   // Treat the Scope.
   clang::NestedNameSpecifier* prefix = 0;
   clang::Qualifiers prefix_qualifiers = instanceType.getLocalQualifiers();
   const clang::ElaboratedType* etype
      = llvm::dyn_cast<clang::ElaboratedType>(instanceType.getTypePtr());
   if (etype) {
      // We have to also handle the prefix.
      prefix = AddDefaultParametersNNS(Ctx, etype->getQualifier(), interpreter, normCtxt);
      instanceType = clang::QualType(etype->getNamedType().getTypePtr(),0);
   }

   // In case of template specializations iterate over the arguments and
   // add unspecified default parameter.

   const clang::TemplateSpecializationType* TST
      = llvm::dyn_cast<const clang::TemplateSpecializationType>(instanceType.getTypePtr());

   const clang::ClassTemplateSpecializationDecl* TSTdecl
      = llvm::dyn_cast_or_null<const clang::ClassTemplateSpecializationDecl>(instanceType.getTypePtr()->getAsCXXRecordDecl());

   if (TST && TSTdecl) {

      bool wantDefault = !cling::utils::Analyze::IsStdOrCompilerDetails(*TSTdecl);

      clang::Sema& S = interpreter.getCI()->getSema();
      clang::TemplateDecl *Template = TSTdecl->getSpecializedTemplate()->getMostRecentDecl();
      clang::TemplateParameterList *Params = Template->getTemplateParameters();
      clang::TemplateParameterList::iterator Param = Params->begin(); // , ParamEnd = Params->end();
      //llvm::SmallVectorImpl<TemplateArgument> Converted; // Need to contains the other arguments.
      // Converted seems to be the same as our 'desArgs'

      bool mightHaveChanged = false;
      llvm::SmallVector<clang::TemplateArgument, 4> desArgs;
      unsigned int Idecl = 0, Edecl = TSTdecl->getTemplateArgs().size();
      for(clang::TemplateSpecializationType::iterator
             I = TST->begin(), E = TST->end();
          Idecl != Edecl;
          I!=E ? ++I : 0, ++Idecl, ++Param) {

         if (I != E) {

            if (I->getKind() == clang::TemplateArgument::Template) {
               clang::TemplateName templateName = I->getAsTemplate();
               clang::TemplateDecl* templateDecl = templateName.getAsTemplateDecl();
               if (templateDecl) {
                  clang::DeclContext* declCtxt = templateDecl->getDeclContext();

                  if (declCtxt && !templateName.getAsQualifiedTemplateName()){
                     clang::NamespaceDecl* ns = clang::dyn_cast<clang::NamespaceDecl>(declCtxt);
                     clang::NestedNameSpecifier* nns;
                     if (ns) nns = cling::utils::TypeName::CreateNestedNameSpecifier(Ctx, ns);
                     else nns = cling::utils::TypeName::CreateNestedNameSpecifier(Ctx,llvm::dyn_cast<clang::TagDecl>(declCtxt),
                                                                                  false /*FullyQualified*/);
                     clang::TemplateName templateNameWithNSS ( Ctx.getQualifiedTemplateName(nns, false, templateDecl) );
                     desArgs.push_back(clang::TemplateArgument(templateNameWithNSS));
                     mightHaveChanged = true;
                     continue;
                  }
               }
            }

            if (I->getKind() != clang::TemplateArgument::Type) {
               desArgs.push_back(*I);
               continue;
            }

            clang::QualType SubTy = I->getAsType();

            // Check if the type needs more desugaring and recurse.
            if (llvm::isa<clang::TemplateSpecializationType>(SubTy)
                || llvm::isa<clang::ElaboratedType>(SubTy) ) {
               mightHaveChanged = true;
               desArgs.push_back(clang::TemplateArgument(AddDefaultParameters(SubTy,
                                                                              interpreter,
                                                                              normCtxt)));
            } else {
               desArgs.push_back(*I);
            }
            // Converted.push_back(TemplateArgument(ArgTypeForTemplate));
         } else if (wantDefault) {

            mightHaveChanged = true;

            const clang::TemplateArgument& templateArg
               = TSTdecl->getTemplateArgs().get(Idecl);
            if (templateArg.getKind() != clang::TemplateArgument::Type) {
               desArgs.push_back(templateArg);
               continue;
            }
            clang::QualType SubTy = templateArg.getAsType();

            clang::SourceLocation TemplateLoc = Template->getSourceRange ().getBegin(); //NOTE: not sure that this is the 'right' location.
            clang::SourceLocation RAngleLoc = TSTdecl->getSourceRange().getBegin(); // NOTE: most likely wrong, I think this is expecting the location of right angle

            clang::TemplateTypeParmDecl *TTP = llvm::dyn_cast<clang::TemplateTypeParmDecl>(*Param);
            {
               // We may induce template instantiation
               cling::Interpreter::PushTransactionRAII clingRAII(const_cast<cling::Interpreter*>(&interpreter));
               clang::sema::HackForDefaultTemplateArg raii;
               bool HasDefaultArgs;
               clang::TemplateArgumentLoc ArgType = S.SubstDefaultTemplateArgumentIfAvailable(
                                                                                              Template,
                                                                                              TemplateLoc,
                                                                                              RAngleLoc,
                                                                                              TTP,
                                                                                              desArgs,
                                                                                              HasDefaultArgs);
               // The substition can fail, in which case there would have been compilation
               // error printed on the screen.
               if (ArgType.getArgument().isNull()
                   || ArgType.getArgument().getKind() != clang::TemplateArgument::Type) {
                  ROOT::TMetaUtils::Error("ROOT::TMetaUtils::AddDefaultParameters",
                                          "Template parameter substitution failed for %s around %s",
                                          instanceType.getAsString().c_str(),
                                          SubTy.getAsString().c_str()
                                          );
                  break;
               }
               clang::QualType BetterSubTy = ArgType.getArgument().getAsType();
               SubTy = cling::utils::Transform::GetPartiallyDesugaredType(Ctx,BetterSubTy,normCtxt.GetConfig(),/*fullyQualified=*/ true);
            }
            SubTy = AddDefaultParameters(SubTy,interpreter,normCtxt);
            desArgs.push_back(clang::TemplateArgument(SubTy));
         } else {
            // We are past the end of the list of specified arguements and we
            // do not want to add the default, no need to continue.
            break;
         }
      }

      // If we added default parameter, allocate new type in the AST.
      if (mightHaveChanged) {
         instanceType = Ctx.getTemplateSpecializationType(TST->getTemplateName(),
                                                          desArgs.data(),
                                                          desArgs.size(),
                                                          TST->getCanonicalTypeInternal());
      }
   }

   if (prefix) {
      instanceType = Ctx.getElaboratedType(clang::ETK_None,prefix,instanceType);
      instanceType = Ctx.getQualifiedType(instanceType,prefix_qualifiers);
   }
   return instanceType;
}

//______________________________________________________________________________
const char* ROOT::TMetaUtils::DataMemberInfo__ValidArrayIndex(const clang::FieldDecl &m, int *errnum, const char **errstr)
{
   // ValidArrayIndex return a static string (so use it or copy it immediatly, do not
   // call GrabIndex twice in the same expression) containing the size of the
   // array data member.
   // In case of error, or if the size is not specified, GrabIndex returns 0.
   // If errnum is not null, *errnum updated with the error number:
   //   Cint::G__DataMemberInfo::G__VALID     : valid array index
   //   Cint::G__DataMemberInfo::G__NOT_INT   : array index is not an int
   //   Cint::G__DataMemberInfo::G__NOT_DEF   : index not defined before array
   //                                          (this IS an error for streaming to disk)
   //   Cint::G__DataMemberInfo::G__IS_PRIVATE: index exist in a parent class but is private
   //   Cint::G__DataMemberInfo::G__UNKNOWN   : index is not known
   // If errstr is not null, *errstr is updated with the address of a static
   //   string containing the part of the index with is invalid.

   llvm::StringRef title;

   // Try to get the comment either from the annotation or the header file if present
   if (clang::AnnotateAttr *A = m.getAttr<clang::AnnotateAttr>())
      title = A->getAnnotation();
   else
      // Try to get the comment from the header file if present
      title = ROOT::TMetaUtils::GetComment( m );

   // Let's see if the user provided us with some information
   // with the format: //[dimension] this is the dim of the array
   // dimension can be an arithmetical expression containing, literal integer,
   // the operator *,+ and - and data member of integral type.  In addition the
   // data members used for the size of the array need to be defined prior to
   // the array.

   if (errnum) *errnum = VALID;

   size_t rightbracket = title.find(']');
   if ((title[0] != '[') ||
       (rightbracket == llvm::StringRef::npos)) return 0;

   std::string working;
   static std::string indexvar;
   indexvar = title.substr(1,rightbracket-1).str();

   // now we should have indexvar=dimension
   // Let's see if this is legal.
   // which means a combination of data member and digit separated by '*','+','-'
   // First we remove white spaces.
   unsigned int i;
   size_t indexvarlen = indexvar.length();
   for ( i=0; i<=indexvarlen; i++) {
      if (!isspace(indexvar[i])) {
         working += indexvar[i];
      }
   };

   // Now we go through all indentifiers
   const char *tokenlist = "*+-";
   char *current = const_cast<char*>(working.c_str());
   current = strtok(current,tokenlist);

   while (current!=0) {
      // Check the token
      if (isdigit(current[0])) {
         for(i=0;i<strlen(current);i++) {
            if (!isdigit(current[0])) {
               // Error we only access integer.
               //NOTE: *** Need to print an error;
               //fprintf(stderr,"*** Datamember %s::%s: size of array (%s) is not an interger\n",
               //        member.MemberOf()->Name(), member.Name(), current);
               if (errstr) *errstr = current;
               if (errnum) *errnum = NOT_INT;
               return 0;
            }
         }
      } else { // current token is not a digit
         // first let's see if it is a data member:
         int found = 0;
         const clang::CXXRecordDecl *parent_clxx = llvm::dyn_cast<clang::CXXRecordDecl>(m.getParent());
         const clang::FieldDecl *index1 = GetDataMemberFromAll(*parent_clxx, current );
         if ( index1 ) {
            if ( IsFieldDeclInt(index1) ) {
               found = 1;
               // Let's see if it has already been written down in the
               // Streamer.
               // Let's see if we already wrote it down in the
               // streamer.
               for(clang::RecordDecl::field_iterator field_iter = parent_clxx->field_begin(), end = parent_clxx->field_end();
                   field_iter != end;
                   ++field_iter)
               {
                  if ( field_iter->getNameAsString() == m.getNameAsString() ) {
                     // we reached the current data member before
                     // reaching the index so we have not written it yet!
                     //NOTE: *** Need to print an error;
                     //fprintf(stderr,"*** Datamember %s::%s: size of array (%s) has not been defined before the array \n",
                     //        member.MemberOf()->Name(), member.Name(), current);
                     if (errstr) *errstr = current;
                     if (errnum) *errnum = NOT_DEF;
                     return 0;
                  }
                  if ( field_iter->getNameAsString() == index1->getNameAsString() ) {
                     break;
                  }
               } // end of while (m_local.Next())
            } else {
               //NOTE: *** Need to print an error;
               //fprintf(stderr,"*** Datamember %s::%s: size of array (%s) is not int \n",
               //        member.MemberOf()->Name(), member.Name(), current);
               if (errstr) *errstr = current;
               if (errnum) *errnum = NOT_INT;
               return 0;
            }
         } else {
            // There is no variable by this name in this class, let see
            // the base classes!:
            index1 = GetDataMemberFromAllParents( *parent_clxx, current );
            if ( index1 ) {
               if ( IsFieldDeclInt(index1) ) {
                  found = 1;
               } else {
                  // We found a data member but it is the wrong type
                  //NOTE: *** Need to print an error;
                  //fprintf(stderr,"*** Datamember %s::%s: size of array (%s) is not int \n",
                  //  member.MemberOf()->Name(), member.Name(), current);
                  if (errnum) *errnum = NOT_INT;
                  if (errstr) *errstr = current;
                  //NOTE: *** Need to print an error;
                  //fprintf(stderr,"*** Datamember %s::%s: size of array (%s) is not int \n",
                  //  member.MemberOf()->Name(), member.Name(), current);
                  if (errnum) *errnum = NOT_INT;
                  if (errstr) *errstr = current;
                  return 0;
               }
               if ( found && (index1->getAccess() == clang::AS_private) ) {
                  //NOTE: *** Need to print an error;
                  //fprintf(stderr,"*** Datamember %s::%s: size of array (%s) is a private member of %s \n",
                  if (errstr) *errstr = current;
                  if (errnum) *errnum = IS_PRIVATE;
                  return 0;
               }
            }
            if (!found) {
               //NOTE: *** Need to print an error;
               //fprintf(stderr,"*** Datamember %s::%s: size of array (%s) is not known \n",
               //        member.MemberOf()->Name(), member.Name(), indexvar);
               if (errstr) *errstr = indexvar.c_str();
               if (errnum) *errnum = UNKNOWN;
               return 0;
            } // end of if not found
         } // end of if is a data member of the class
      } // end of if isdigit

      current = strtok(0,tokenlist);
   } // end of while loop on tokens

   return indexvar.c_str();

}

//______________________________________________________________________________
void ROOT::TMetaUtils::GetCppName(std::string &out, const char *in)
{
   // Return (in the argument 'output') a mangled version of the C++ symbol/type (pass as 'input')
   // that can be used in C++ as a variable name.

   out.resize(strlen(in)*2);
   unsigned int i=0,j=0,c;
   while((c=in[i])) {
      if (out.capacity() < (j+3)) {
         out.resize(2*j+3);
      }
      switch(c) { // We resized the underlying buffer if needed
         case '+': strcpy(const_cast<char*>(out.data())+j,"pL"); j+=2; break;
         case '-': strcpy(const_cast<char*>(out.data())+j,"mI"); j+=2; break;
         case '*': strcpy(const_cast<char*>(out.data())+j,"mU"); j+=2; break;
         case '/': strcpy(const_cast<char*>(out.data())+j,"dI"); j+=2; break;
         case '&': strcpy(const_cast<char*>(out.data())+j,"aN"); j+=2; break;
         case '%': strcpy(const_cast<char*>(out.data())+j,"pE"); j+=2; break;
         case '|': strcpy(const_cast<char*>(out.data())+j,"oR"); j+=2; break;
         case '^': strcpy(const_cast<char*>(out.data())+j,"hA"); j+=2; break;
         case '>': strcpy(const_cast<char*>(out.data())+j,"gR"); j+=2; break;
         case '<': strcpy(const_cast<char*>(out.data())+j,"lE"); j+=2; break;
         case '=': strcpy(const_cast<char*>(out.data())+j,"eQ"); j+=2; break;
         case '~': strcpy(const_cast<char*>(out.data())+j,"wA"); j+=2; break;
         case '.': strcpy(const_cast<char*>(out.data())+j,"dO"); j+=2; break;
         case '(': strcpy(const_cast<char*>(out.data())+j,"oP"); j+=2; break;
         case ')': strcpy(const_cast<char*>(out.data())+j,"cP"); j+=2; break;
         case '[': strcpy(const_cast<char*>(out.data())+j,"oB"); j+=2; break;
         case ']': strcpy(const_cast<char*>(out.data())+j,"cB"); j+=2; break;
         case '!': strcpy(const_cast<char*>(out.data())+j,"nO"); j+=2; break;
         case ',': strcpy(const_cast<char*>(out.data())+j,"cO"); j+=2; break;
         case '$': strcpy(const_cast<char*>(out.data())+j,"dA"); j+=2; break;
         case ' ': strcpy(const_cast<char*>(out.data())+j,"sP"); j+=2; break;
         case ':': strcpy(const_cast<char*>(out.data())+j,"cL"); j+=2; break;
         case '"': strcpy(const_cast<char*>(out.data())+j,"dQ"); j+=2; break;
         case '@': strcpy(const_cast<char*>(out.data())+j,"aT"); j+=2; break;
         case '\'': strcpy(const_cast<char*>(out.data())+j,"sQ"); j+=2; break;
         case '\\': strcpy(const_cast<char*>(out.data())+j,"fI"); j+=2; break;
         default: out[j++]=c; break;
      }
      ++i;
   }
   out.resize(j);

   // Remove initial numbers if any
   std::size_t firstNonNumber = out.find_first_not_of("0123456789");
   out.replace(0,firstNonNumber,"");   

   return;
}

static clang::SourceLocation
getFinalSpellingLoc(clang::SourceManager& sourceManager,
                    clang::SourceLocation sourceLoc) {
   // Follow macro expansion until we hit a source file.
   if (!sourceLoc.isFileID()) {
      return sourceManager.getExpansionRange(sourceLoc).second;
   }
   return sourceLoc;
}

//______________________________________________________________________________
llvm::StringRef ROOT::TMetaUtils::GetFileName(const clang::Decl *decl,
                                              const cling::Interpreter& interp)
{
   // Return the header file to be included to declare the Decl.

   // It looks like the template specialization decl actually contains _less_ information
   // on the location of the code than the decl (in case where there is forward declaration,
   // that is what the specialization points to).
   //
   // const clang::CXXRecordDecl* clxx = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
   // if (clxx) {
   //    switch(clxx->getTemplateSpecializationKind()) {
   //       case clang::TSK_Undeclared:
   //          // We want the default behavior
   //          break;
   //       case clang::TSK_ExplicitInstantiationDeclaration:
   //       case clang::TSK_ExplicitInstantiationDefinition:
   //       case clang::TSK_ImplicitInstantiation: {
   //          // We want the location of the template declaration:
   //          const clang::ClassTemplateSpecializationDecl *tmplt_specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl> (clxx);
   //          if (tmplt_specialization) {
   //             // return GetFileName(const_cast< clang::ClassTemplateSpecializationDecl *>(tmplt_specialization)->getSpecializedTemplate());
   //          }
   //          break;
   //       }
   //       case clang::TSK_ExplicitSpecialization:
   //          // We want the default behavior
   //          break;
   //       default:
   //          break;
   //    }
   // }

   using namespace clang;
   SourceLocation headerLoc = decl->getLocation();

   static const char invalidFilename[] = "invalid";
   if (!headerLoc.isValid()) return invalidFilename;

   SourceManager& sourceManager = decl->getASTContext().getSourceManager();
   headerLoc = getFinalSpellingLoc(sourceManager, headerLoc);
   FileID headerFID = sourceManager.getFileID(headerLoc);
   SourceLocation includeLoc
      = getFinalSpellingLoc(sourceManager,
                            sourceManager.getIncludeLoc(headerFID));


   while (includeLoc.isValid() && sourceManager.isInSystemHeader(includeLoc)) {
      headerFID = sourceManager.getFileID(includeLoc);
      includeLoc = getFinalSpellingLoc(sourceManager,
                                       sourceManager.getIncludeLoc(headerFID));
   }

   const FileEntry* headerFE = sourceManager.getFileEntryForID(headerFID);
   if (!headerFE) return invalidFilename;
   llvm::StringRef headerFileName = headerFE->getName();
   HeaderSearch& HdrSearch = interp.getCI()->getPreprocessor().getHeaderSearchInfo();

   // Now headerFID references the last valid system header or the original
   // user file.
   // Find out how to include it by matching file name to include paths.
   // We assume that the file "/A/B/C/D.h" can at some level be included as
   // "C/D.h". Be we cannot know whether that happens to be a different file
   // with the same name. Thus we first find the longest stem that can be
   // reached, say B/C/D.h. Then we find the shortest one, say C/D.h, that
   // points to the same file as the long version. If such a short version
   // exists it will be returned. If it doesn't the long version is returned.
   bool isAbsolute = llvm::sys::path::is_absolute(headerFileName);
   const FileEntry* FELong = 0;
   // Find the longest available match.
   for (llvm::sys::path::const_iterator
           IDir = llvm::sys::path::begin(headerFileName),
           EDir = llvm::sys::path::end(headerFileName);
        !FELong && IDir != EDir; ++IDir) {
      if (isAbsolute) {
         // skip "/" part
         isAbsolute = false;
         continue;
      }
      size_t lenTrailing = headerFileName.size() - (IDir->data() - headerFileName.data());
      llvm::StringRef trailingPart(IDir->data(), lenTrailing);
      assert(trailingPart.data() + trailingPart.size()
             == headerFileName.data() + headerFileName.size()
             && "Mismatched partitioning of file name!");
      const DirectoryLookup* FoundDir = 0;
      FELong = HdrSearch.LookupFile(trailingPart, true /*isAngled*/,
                                    0/*FromDir*/, FoundDir, 0/*CurFileEntry*/,
                                    0/*Searchpath*/, 0/*RelPath*/,
                                    0/*SuggModule*/);
   }

   if (!FELong) {
      // We did not find any file part in any search path.
      return invalidFilename;
   }

   // Iterates through path *parts* "C"; we need trailing parts "C/D.h"
   for (llvm::sys::path::reverse_iterator
           IDir = llvm::sys::path::rbegin(headerFileName),
           EDir = llvm::sys::path::rend(headerFileName);
        IDir != EDir; ++IDir) {
      size_t lenTrailing = headerFileName.size() - (IDir->data() - headerFileName.data());
      llvm::StringRef trailingPart(IDir->data(), lenTrailing);
      assert(trailingPart.data() + trailingPart.size()
             == headerFileName.data() + headerFileName.size()
             && "Mismatched partitioning of file name!");
      const DirectoryLookup* FoundDir = 0;
      // Can we find it, and is it the same file as the long version?
      // (or are we back to the previously found spelling, which is fine, too)
      if (HdrSearch.LookupFile(trailingPart, true /*isAngled*/, 0/*FromDir*/,
                               FoundDir, 0/*CurFileEntry*/, 0/*Searchpath*/,
                               0/*RelPath*/, 0/*SuggModule*/) == FELong) {
         return trailingPart;
      }
   }

   return invalidFilename;
}

//______________________________________________________________________________
void ROOT::TMetaUtils::GetFullyQualifiedTypeName(std::string &typenamestr,
                                                 const clang::QualType &qtype,
                                                 const clang::ASTContext &astContext)
{
   std::string fqname = cling::utils::TypeName::GetFullyQualifiedName(qtype, astContext);
   TClassEdit::TSplitType splitname(fqname.c_str(),
                                    (TClassEdit::EModType)(TClassEdit::kLong64 | TClassEdit::kDropStd | TClassEdit::kDropStlDefault | TClassEdit::kKeepOuterConst));
   splitname.ShortType(typenamestr,TClassEdit::kDropStd | TClassEdit::kDropStlDefault | TClassEdit::kKeepOuterConst);
}

//______________________________________________________________________________
void ROOT::TMetaUtils::GetFullyQualifiedTypeName(std::string &typenamestr,
                                                 const clang::QualType &qtype,
                                                 const cling::Interpreter &interpreter)
{

   GetFullyQualifiedTypeName(typenamestr,
                             qtype,
                             interpreter.getCI()->getASTContext());
}

//______________________________________________________________________________
std::string ROOT::TMetaUtils::GetInterpreterExtraIncludePath(bool rootbuild)
{
   // Return the -I needed to find RuntimeUniverse.h
   if (!rootbuild) {
#ifndef ROOTETCDIR
      const char* rootsys = getenv("ROOTSYS");
      if (!rootsys) {
         Error(0, "Environment variable ROOTSYS not set!");
         return "-Ietc";
      }
      return std::string("-I") + rootsys + "/etc";
#else
      return std::string("-I") + ROOTETCDIR;
#endif
   }
   // else
   return "-Ietc";
}

//______________________________________________________________________________
std::string ROOT::TMetaUtils::GetLLVMResourceDir(bool rootbuild)
{
   // Return the LLVM / clang resource directory
#ifdef R__EXTERN_LLVMDIR
   return R__EXTERN_LLVMDIR;
#else
   return GetInterpreterExtraIncludePath(rootbuild)
      .substr(2, std::string::npos) + "/cling";
#endif
}

//______________________________________________________________________________
void ROOT::TMetaUtils::GetNormalizedName(std::string &norm_name, const clang::QualType &type, const cling::Interpreter &interpreter, const TNormalizedCtxt &normCtxt)
{
   // Return the type name normalized for ROOT,
   // keeping only the ROOT opaque typedef (Double32_t, etc.) and
   // adding default template argument for all types except the STL collections
   // where we remove the default template argument if any.
   //
   // This routine might actually belong in the interpreter because
   // cache the clang::Type might be intepreter specific.

   if (type.isNull()) {
      norm_name = "";
      return;
   }

   clang::ASTContext &ctxt = interpreter.getCI()->getASTContext();

   clang::QualType normalizedType = cling::utils::Transform::GetPartiallyDesugaredType(ctxt, type, normCtxt.GetConfig(), true /* fully qualify */);

   // Readd missing default template parameter.
   normalizedType = ROOT::TMetaUtils::AddDefaultParameters(normalizedType, interpreter, normCtxt);

   clang::PrintingPolicy policy(ctxt.getPrintingPolicy());
   policy.SuppressTagKeyword = true; // Never get the class or struct keyword
   policy.SuppressScope = true;      // Force the scope to be coming from a clang::ElaboratedType.
   policy.AnonymousTagLocations = false; // Do not extract file name + line number for anonymous types.
   // The scope suppression is required for getting rid of the anonymous part of the name of a class defined in an anonymous namespace.
   // This gives us more control vs not using the clang::ElaboratedType and relying on the Policy.SuppressUnwrittenScope which would
   // strip both the anonymous and the inline namespace names (and we probably do not want the later to be suppressed).

   std::string normalizedNameStep1;
   normalizedType.getAsStringInternal(normalizedNameStep1,policy);

   // Still remove the std:: and default template argument and insert the Long64_t and change basic_string to string.
   TClassEdit::TSplitType splitname(normalizedNameStep1.c_str(),(TClassEdit::EModType)(TClassEdit::kLong64 | TClassEdit::kDropStd | TClassEdit::kDropStlDefault | TClassEdit::kKeepOuterConst));
   splitname.ShortType(norm_name,TClassEdit::kDropStd | TClassEdit::kDropStlDefault );

   // The result of this routine is by definition a fully qualified name.  There is an implicit starting '::' at the beginning of the name.
   // Depending on how the user typed his/her code, in particular typedef declarations, we may end up with an explicit '::' being
   // part of the result string.  For consistency, we must remove it.
   if (norm_name.length()>2 && norm_name[0]==':' && norm_name[1]==':') {
      norm_name.erase(0,2);
   }

}

//______________________________________________________________________________
void ROOT::TMetaUtils::GetNormalizedName(std::string &norm_name,
                                         const clang::TypeDecl* typeDecl,
                                         const cling::Interpreter &interpreter)
{
   ROOT::TMetaUtils::TNormalizedCtxt tNormCtxt(interpreter.getLookupHelper());
   const clang::Sema &sema = interpreter.getSema();
   clang::ASTContext& astCtxt = sema.getASTContext();
   clang::QualType qualType = astCtxt.getTypeDeclType(typeDecl);
   
   ROOT::TMetaUtils::GetNormalizedName(norm_name,
                                       qualType,
                                       interpreter,
                                       tNormCtxt);
}

//______________________________________________________________________________
std::string ROOT::TMetaUtils::GetROOTIncludeDir(bool rootbuild)
{
   const std::string defaultInclude ("include");
   if (!rootbuild) {
#ifndef ROOTINCDIR
      const char* rootSysContent = getenv("ROOTSYS");
      if (rootSysContent) {
         std::string incl_rootsys (rootSysContent);
         return incl_rootsys + "/" + defaultInclude;
      } else {
         Error(0,"Environment variable ROOTSYS not set");
         return defaultInclude;
      }
#else
      return ROOTINCDIR;
#endif
   }

   return defaultInclude;
}

//______________________________________________________________________________
std::string ROOT::TMetaUtils::GetModuleFileName(const char* moduleName)
{
   // Return the dictionary file name for a module
   std::string dictFileName(moduleName);
   dictFileName += "_rdict.pcm";
   return dictFileName;
}

//______________________________________________________________________________
clang::Module* ROOT::TMetaUtils::declareModuleMap(clang::CompilerInstance* CI,
                                                  const char* moduleFileName,
                                                  const char* headers[])
{
   // Declare a virtual module.map to clang. Returns Module on success.
   clang::Preprocessor& PP = CI->getPreprocessor();
   clang::ModuleMap& ModuleMap = PP.getHeaderSearchInfo().getModuleMap();

   // Set the patch for searching for modules
   clang::HeaderSearch& HS = CI->getPreprocessor().getHeaderSearchInfo();
   HS.setModuleCachePath(llvm::sys::path::parent_path(moduleFileName));

   llvm::StringRef moduleName = llvm::sys::path::filename(moduleFileName);
   moduleName = llvm::sys::path::stem(moduleName);

   std::pair<clang::Module*, bool> modCreation;

   modCreation
      = ModuleMap.findOrCreateModule(moduleName.str().c_str(),
                                     0 /*ActiveModule*/,
                                     false /*Framework*/, false /*Explicit*/);
   if (!modCreation.second && !strstr(moduleFileName, "/allDict_rdict.pcm")) {
      std::cerr << "TMetaUtils::declareModuleMap: "
         "Duplicate definition of dictionary module "
                << moduleFileName << std::endl;
            /*"\nOriginal module was found in %s.", - if only we could...*/
      // Go on, add new headers nonetheless.
   }

   clang::HeaderSearch& HdrSearch = PP.getHeaderSearchInfo();
   for (const char** hdr = headers; hdr && *hdr; ++hdr) {
      const clang::DirectoryLookup* CurDir;
      const clang::FileEntry* hdrFileEntry
         =  HdrSearch.LookupFile(*hdr, false /*isAngled*/, 0 /*FromDir*/,
                                 CurDir, 0 /*CurFileEnt*/, 0 /*SearchPath*/,
                                 0 /*RelativePath*/, 0 /*SuggestedModule*/);
      if (!hdrFileEntry) {
         std::cerr << "TMetaUtils::declareModuleMap: "
            "Cannot find header file " << *hdr
                   << " included in dictionary module "
                   << moduleName.data()
                   << " in include search path!";
         hdrFileEntry = PP.getFileManager().getFile(*hdr, /*OpenFile=*/false,
                                                    /*CacheFailure=*/false);
      } else if (getenv("ROOT_MODULES")) {
         // Tell HeaderSearch that the header's directory has a module.map
         llvm::StringRef srHdrDir(hdrFileEntry->getName());
         srHdrDir = llvm::sys::path::parent_path(srHdrDir);
         const clang::DirectoryEntry* Dir
            = PP.getFileManager().getDirectory(srHdrDir);
         if (Dir) {
            HdrSearch.setDirectoryHasModuleMap(Dir);
         }
      }

      ModuleMap.addHeader(modCreation.first, hdrFileEntry,
                          clang::ModuleMap::NormalHeader);
   } // for headers
   return modCreation.first;
}

int dumpDeclForAssert(const clang::Decl& D, const char* commentStart) {
   llvm::errs() << llvm::StringRef(commentStart, 80) << '\n';
   D.dump();
   return 0;
}

//______________________________________________________________________________
llvm::StringRef ROOT::TMetaUtils::GetComment(const clang::Decl &decl, clang::SourceLocation *loc)
{
   // Returns the comment (// striped away), annotating declaration in a meaningful
   // for ROOT IO way.
   // Takes optional out parameter clang::SourceLocation returning the source
   // location of the comment.
   //
   // CXXMethodDecls, FieldDecls and TagDecls are annotated.
   // CXXMethodDecls declarations and FieldDecls are annotated as follows:
   // Eg. void f(); // comment1
   //     int member; // comment2
   // Inline definitions of CXXMethodDecls after the closing } \n. Eg:
   // void f()
   // {...}  // comment3
   // TagDecls are annotated in the end of the ClassDef macro. Eg.
   // class MyClass {
      // ...
      // ClassDef(MyClass, 1) // comment4
      //

   clang::SourceManager& sourceManager = decl.getASTContext().getSourceManager();
   clang::SourceLocation sourceLocation = decl.getLocEnd();

   // If the location is a macro get the expansion location.
   sourceLocation = sourceManager.getExpansionRange(sourceLocation).second;

   bool invalid;
   const char *commentStart = sourceManager.getCharacterData(sourceLocation, &invalid);
   if (invalid)
      return "";

   bool skipToSemi = true;
   if (const clang::FunctionDecl* FD = clang::dyn_cast<clang::FunctionDecl>(&decl)) {
      if (FD->isImplicit()) {
         // Compiler generated function.
         return "";
      }
      if (FD->isExplicitlyDefaulted() || FD->isDeletedAsWritten()) {
         // ctorOrFunc() = xyz; with commentStart pointing somewhere into
         // ctorOrFunc.
         // We have to skipToSemi
      } else if (FD->doesThisDeclarationHaveABody()) {
         // commentStart is at body's '}'
         // But we might end up e.g. at the ')' of a CPP macro
         assert((decl.getLocEnd() != sourceLocation || *commentStart == '}'
                 || dumpDeclForAssert(*FD, commentStart))
                && "Expected macro or end of body at '}'");
         if (*commentStart) ++commentStart;

         // We might still have a ';'; skip the spaces and check.
         while (*commentStart && isspace(*commentStart)
                && *commentStart != '\n' && *commentStart != '\r') {
            ++commentStart;
         }
         if (*commentStart == ';') ++commentStart;

         skipToSemi = false;
      }
   } else if (const clang::EnumConstantDecl* ECD
              = clang::dyn_cast<clang::EnumConstantDecl>(&decl)) {
      // either "konstant = 12, //COMMENT" or "lastkonstant // COMMENT"
      if (ECD->getNextDeclInContext())
         while (*commentStart && *commentStart != ',' && *commentStart != '\r' && *commentStart != '\n')
            ++commentStart;
      // else commentStart already points to the end.

      skipToSemi = false;
   }

   if (skipToSemi) {
      while (*commentStart && *commentStart != ';' && *commentStart != '\r' && *commentStart != '\n')
         ++commentStart;
      if (*commentStart == ';') ++commentStart;
   }

   // Now skip the spaces until beginning of comments or EOL.
   while ( *commentStart && isspace(*commentStart)
           && *commentStart != '\n' && *commentStart != '\r') {
      ++commentStart;
   }

   if (commentStart[0] != '/' ||
       (commentStart[1] != '/' && commentStart[1] != '*')) {
      // not a comment
      return "";
   }
   commentStart += 2;

   // Now skip the spaces after comment start until EOL.
   while ( *commentStart && isspace(*commentStart)
           && *commentStart != '\n' && *commentStart != '\r') {
      ++commentStart;
   }
   const char* commentEnd = commentStart;
   // Even for /* comments we only take the first line into account.
   while (*commentEnd && *commentEnd != '\n' && *commentEnd != '\r') {
      ++commentEnd;
   }

   // "Skip" (don't include) trailing space.
   // *commentEnd points behind comment end thus check commentEnd[-1]
   while (commentEnd > commentStart && isspace(commentEnd[-1])) {
      --commentEnd;
   }

   if (loc) {
      // Find the true beginning of a comment.
      unsigned offset = commentStart - sourceManager.getCharacterData(sourceLocation);
      *loc = sourceLocation.getLocWithOffset(offset - 1);
   }

   return llvm::StringRef(commentStart, commentEnd - commentStart);
}

//______________________________________________________________________________
llvm::StringRef ROOT::TMetaUtils::GetClassComment(const clang::CXXRecordDecl &decl, clang::SourceLocation *loc, const cling::Interpreter &interpreter)
{
   // Return the class comment after the ClassDef:
   // class MyClass {
      // ...
      // ClassDef(MyClass, 1) // class comment
      //

   using namespace clang;
   SourceLocation commentSLoc;
   llvm::StringRef comment;

   Sema& sema = interpreter.getCI()->getSema();

   const Decl* DeclFileLineDecl
      = interpreter.getLookupHelper().findFunctionProto(&decl, "DeclFileLine", "");
   if (!DeclFileLineDecl) return llvm::StringRef();

   // For now we allow only a special macro (ClassDef) to have meaningful comments
   SourceLocation maybeMacroLoc = DeclFileLineDecl->getLocation();
   bool isClassDefMacro = maybeMacroLoc.isMacroID() && sema.findMacroSpelling(maybeMacroLoc, "ClassDef");
   if (isClassDefMacro) {
      comment = ROOT::TMetaUtils::GetComment(*DeclFileLineDecl, &commentSLoc);
      if (comment.size()) {
         if (loc){
            *loc = commentSLoc;
         }
         return comment;
      }
   }
   return llvm::StringRef();
}

//______________________________________________________________________________
const clang::Type *ROOT::TMetaUtils::GetUnderlyingType(clang::QualType type)
{
   // Return the base/underlying type of a chain of array or pointers type.
   // Does not yet support the array and pointer part being intermixed.

   const clang::Type *rawtype = type.getTypePtr();

   // NOTE: We probably meant isa<clang::ElaboratedType>
   if (rawtype->isElaboratedTypeSpecifier() ) {
      rawtype = rawtype->getCanonicalTypeInternal().getTypePtr();
   }
   if (rawtype->isArrayType()) {
      rawtype = type.getTypePtr()->getBaseElementTypeUnsafe ();
   }
   if (rawtype->isPointerType() || rawtype->isReferenceType() ) {
      //Get to the 'raw' type.
      clang::QualType pointee;
      while ( (pointee = rawtype->getPointeeType()) , pointee.getTypePtrOrNull() && pointee.getTypePtr() != rawtype)
      {
         rawtype = pointee.getTypePtr();

         if (rawtype->isElaboratedTypeSpecifier() ) {
            rawtype = rawtype->getCanonicalTypeInternal().getTypePtr();
         }
         if (rawtype->isArrayType()) {
            rawtype = rawtype->getBaseElementTypeUnsafe ();
         }
      }
   }
   if (rawtype->isArrayType()) {
      rawtype = rawtype->getBaseElementTypeUnsafe ();
   }
   return rawtype;
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::IsStdClass(const clang::RecordDecl &cl)
{
   // Return true, if the decl is part of the std namespace.

   const clang::DeclContext *ctx = cl.getDeclContext();

   while (ctx && ctx->isInlineNamespace()) {
      ctx = ctx->getParent();
   }

   if (ctx && ctx->isNamespace())
   {
      const clang::NamedDecl *parent = llvm::dyn_cast<clang::NamedDecl> (ctx);
      if (parent) {
         if (parent->getDeclContext()->isTranslationUnit()
             && parent->getQualifiedNameAsString()=="std") {
            return true;
         }
      }
   }
   return false;
}

//______________________________________________________________________________
bool ROOT::TMetaUtils::MatchWithDeclOrAnyOfPrevious(const clang::CXXRecordDecl &cl,
                                                    const clang::CXXRecordDecl &currentCl)
{
   // This is a recursive function

   // We found it: let's return true
   if (&cl == &currentCl) return true;

   const  clang::CXXRecordDecl* previous = currentCl.getPreviousDecl();

   // There is no previous decl, so we cannot possibly find it
   if (NULL == previous){
      return false;
   }

   // We try to find it in the previous
   return ROOT::TMetaUtils::MatchWithDeclOrAnyOfPrevious(cl, *previous);

}

//______________________________________________________________________________

bool ROOT::TMetaUtils::IsOfType(const clang::CXXRecordDecl &cl, const std::string& typ, const cling::LookupHelper& lh)
{
   // Return true if the decl is of type.
   // A proper hashtable for caching results would be the ideal solution
   // 1) Only one lookup per type
   // 2) No string comparison
   // We may use a map which becomes an unordered map if c++11 is enabled?

   const clang::CXXRecordDecl *thisDecl =
      llvm::dyn_cast_or_null<clang::CXXRecordDecl>(lh.findScope(typ));

   // this would be probably an assert given that this state is not reachable unless a mistake is somewhere
   if (! thisDecl){
      Error("IsOfType","Record decl of type %s not found in the AST.", typ.c_str());
      return false;
      }

   // Now loop on all previous decls to seek a match
   const clang::CXXRecordDecl *mostRecentDecl = thisDecl->getMostRecentDecl();
   bool matchFound = MatchWithDeclOrAnyOfPrevious (cl,*mostRecentDecl);

   return matchFound;
}

//______________________________________________________________________________
ROOT::ESTLType ROOT::TMetaUtils::IsSTLCont(const clang::RecordDecl &cl)
{
   //  type     : type name: vector<list<classA,allocator>,allocator>
   //  result:    0          : not stl container
   //             abs(result): code of container 1=vector,2=list,3=deque,4=map
   //                           5=multimap,6=set,7=multiset

   // This routine could be enhanced to also support:
   //
   //  testAlloc: if true, we test allocator, if it is not default result is negative
   //  result:    0          : not stl container
   //             abs(result): code of container 1=vector,2=list,3=deque,4=map
   //                           5=multimap,6=set,7=multiset
   //             positive val: we have a vector or list with default allocator to any depth
   //                   like vector<list<vector<int>>>
   //             negative val: STL container other than vector or list, or non default allocator
   //                           For example: vector<deque<int>> has answer -1

   if (!IsStdClass(cl)) {
      return ROOT::kNotSTL;
   }

   return STLKind(cl.getName());
}

//______________________________________________________________________________
clang::QualType ROOT::TMetaUtils::ReSubstTemplateArg(clang::QualType input, const clang::Type *instance)
{
   // Check if 'input' or any of its template parameter was substituted when
   // instantiating the class template instance and replace it with the
   // partially sugared types we have from 'instance'.

   if (!instance) return input;

   // Treat scope (clang::ElaboratedType) if any.
   const clang::ElaboratedType* etype
      = llvm::dyn_cast<clang::ElaboratedType>(input.getTypePtr());
   if (etype) {
      // We have to also handle the prefix.

      clang::Qualifiers scope_qualifiers = input.getLocalQualifiers();
      assert(instance->getAsCXXRecordDecl()!=0 && "ReSubstTemplateArg only makes sense with a type representing a class.");
      const clang::ASTContext &Ctxt = instance->getAsCXXRecordDecl()->getASTContext();

      clang::NestedNameSpecifier *scope = ReSubstTemplateArgNNS(Ctxt,etype->getQualifier(),instance);
      clang::QualType subTy = ReSubstTemplateArg(clang::QualType(etype->getNamedType().getTypePtr(),0),instance);

      if (scope) subTy = Ctxt.getElaboratedType(clang::ETK_None,scope,subTy);
      subTy = Ctxt.getQualifiedType(subTy,scope_qualifiers);
      return subTy;
   }

   // If the instance is also an elaborated type, we need to skip
   etype = llvm::dyn_cast<clang::ElaboratedType>(instance);
   if (etype) {
      instance = etype->getNamedType().getTypePtr();
      if (!instance) return input;
   }

   const clang::TemplateSpecializationType* TST
      = llvm::dyn_cast<const clang::TemplateSpecializationType>(instance);

   if (!TST) return input;

   const clang::ClassTemplateSpecializationDecl* TSTdecl
      = llvm::dyn_cast_or_null<const clang::ClassTemplateSpecializationDecl>(instance->getAsCXXRecordDecl());

   const clang::SubstTemplateTypeParmType *substType
      = llvm::dyn_cast<clang::SubstTemplateTypeParmType>(input.getTypePtr());

   if (substType) {
      // Make sure it got replaced from this template
      const clang::ClassTemplateDecl *replacedCtxt = 0;

      const clang::DeclContext *replacedDeclCtxt = substType->getReplacedParameter()->getDecl()->getDeclContext();
      const clang::CXXRecordDecl *decl = llvm::dyn_cast<clang::CXXRecordDecl>(replacedDeclCtxt);
      if (decl) {
         if (decl->getKind() == clang::Decl::ClassTemplatePartialSpecialization) {
            replacedCtxt = llvm::dyn_cast<clang::ClassTemplatePartialSpecializationDecl>(decl)->getSpecializedTemplate();
         } else {
            replacedCtxt = decl->getDescribedClassTemplate();
         }
      } else {
         replacedCtxt = llvm::dyn_cast<clang::ClassTemplateDecl>(replacedDeclCtxt);
      }
      unsigned int index = substType->getReplacedParameter()->getIndex();
      if (replacedCtxt->getCanonicalDecl() == TSTdecl->getSpecializedTemplate()->getCanonicalDecl()
          || /* the following is likely just redundant */
          substType->getReplacedParameter()->getDecl()
          == TSTdecl->getSpecializedTemplate ()->getTemplateParameters()->getParam(index))
      {
         if ( index >= TST->getNumArgs() ) {
            // The argument replaced was a default template argument that is
            // being listed as part of the instance ...
            // so we probably don't really know how to spell it ... we would need to recreate it
            // (See AddDefaultParamters).
            return input;
         } else {
            return TST->getArg(index).getAsType();
         }
      }
   }
   // Maybe a class template instance, recurse and rebuild
   const clang::TemplateSpecializationType* inputTST
      = llvm::dyn_cast<const clang::TemplateSpecializationType>(input.getTypePtr());
   const clang::ASTContext& astCtxt = TSTdecl->getASTContext();

   if (inputTST) {
      bool mightHaveChanged = false;
      llvm::SmallVector<clang::TemplateArgument, 4> desArgs;
      for(clang::TemplateSpecializationType::iterator I = inputTST->begin(), E = inputTST->end();
          I != E; ++I) {
         if (I->getKind() != clang::TemplateArgument::Type) {
            desArgs.push_back(*I);
            continue;
         }

         clang::QualType SubTy = I->getAsType();
         // Check if the type needs more desugaring and recurse.
         if (llvm::isa<clang::SubstTemplateTypeParmType>(SubTy)
             || llvm::isa<clang::TemplateSpecializationType>(SubTy)) {
            mightHaveChanged = true;
            clang::QualType newSubTy = ReSubstTemplateArg(SubTy,instance);
            if (!newSubTy.isNull()) {
               desArgs.push_back(clang::TemplateArgument(newSubTy));
            }
         } else
            desArgs.push_back(*I);
      }

      // If desugaring happened allocate new type in the AST.
      if (mightHaveChanged) {
         clang::Qualifiers qualifiers = input.getLocalQualifiers();
         input = astCtxt.getTemplateSpecializationType(inputTST->getTemplateName(),
                                                       desArgs.data(),
                                                       desArgs.size(),
                                                       inputTST->getCanonicalTypeInternal());
         input = astCtxt.getQualifiedType(input, qualifiers);
      }
   }

   return input;
}

//______________________________________________________________________________
ROOT::ESTLType ROOT::TMetaUtils::STLKind(const llvm::StringRef type)
{
   // Converts STL container name to number. vector -> 1, etc..

   static const char *stls[] =                  //container names
      {"any","vector","list","deque","map","multimap","set","multiset","bitset",0};
   static const ROOT::ESTLType values[] =
      {ROOT::kNotSTL, ROOT::kSTLvector,
       ROOT::kSTLlist, ROOT::kSTLdeque,
       ROOT::kSTLmap, ROOT::kSTLmultimap,
       ROOT::kSTLset, ROOT::kSTLmultiset,
       ROOT::kSTLbitset, ROOT::kNotSTL
      };
   //              kind of stl container
   for(int k=1;stls[k];k++) {if (type.equals(stls[k])) return values[k];}
   return ROOT::kNotSTL;
}

//______________________________________________________________________________
const clang::TypedefNameDecl* ROOT::TMetaUtils::GetAnnotatedRedeclarable(const clang::TypedefNameDecl* TND)
{
   if (!TND)
      return 0;

   TND = TND->getMostRecentDecl();
   while (TND && !(TND->hasAttrs()))
      TND = TND->getPreviousDecl();

   return TND;
}

//______________________________________________________________________________
const clang::TagDecl* ROOT::TMetaUtils::GetAnnotatedRedeclarable(const clang::TagDecl* TD)
{
   if (!TD)
      return 0;

   TD = TD->getMostRecentDecl();
   while (TD && !(TD->hasAttrs() && TD->isThisDeclarationADefinition()))
      TD = TD->getPreviousDecl();

   return TD;
}

//______________________________________________________________________________
void ROOT::TMetaUtils::SetPathsForRelocatability(std::vector<std::string>& clingArgs )
{
   // Organise the parameters for cling in order to guarantee relocatability
   // It treats the gcc toolchain and the root include path
   // FIXME: enables relocatability for experiments' framework headers until PCMs
   // are available.
   const char* envInclPath = getenv("ROOT_INCLUDE_PATH");

   if (envInclPath) {
      std::istringstream envInclPathsStream(envInclPath);
      std::string inclPath;
      while (std::getline(envInclPathsStream, inclPath, ':')) {
         clingArgs.push_back("-I");
         clingArgs.push_back(inclPath);
      }
   }
}
