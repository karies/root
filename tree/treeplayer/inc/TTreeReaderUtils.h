// @(#)root/tree:$Id$
// Author: Axel Naumann, 2010-10-12

/*************************************************************************
 * Copyright (C) 1995-2013, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TTreeReaderUtils
#define ROOT_TTreeReaderUtils


////////////////////////////////////////////////////////////////////////////
//                                                                        //
// TTreeReaderUtils                                                       //
//                                                                        //
// TTreeReader's helpers.                                                 //
//                                                                        //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

#include "TBranchProxyDirector.h"
#include "TBranchProxy.h"
#include "TTreeReaderValue.h"

class TDictionary;
class TTree;

namespace ROOT {
   namespace Detail {
      class TBranchProxy;
   }

namespace Internal {
   class TBranchProxyDirector;
   class TTreeReaderArrayBase;

   class TNamedBranchProxy: public TObject {
   public:
      TNamedBranchProxy() = default;

      TNamedBranchProxy(TBranchProxyDirector* boss, TBranch* branch, const char* membername):
         fProxy(new TBranchProxy(boss, branch, membername)) {}

      template <class PROXY>
      TNamedBranchProxy(TBranchProxyDirector* boss, TBranch* branch, const char* membername):
         fProxy(new PROXY(boss, branch, membername)) {}

      const char* GetName() const { return fProxy->GetBranchName(); }
      const Detail::TBranchProxy* GetProxy() const { return fProxy.get(); }
      Detail::TBranchProxy* GetProxy() { return fProxy.get(); }
      TDictionary* GetDict() const { return fDict; }
      void SetDict(TDictionary* dict) { fDict = dict; }
      TDictionary* GetContentDict() const { return fContentDict; }
      void SetContentDict(TDictionary* dict) { fContentDict = dict; }

   private:
      std::unique_ptr<Detail::TBranchProxy> fProxy;
      TDictionary*         fDict = 0;
      TDictionary*         fContentDict = 0; // type of content, if a collection
      ClassDef(TNamedBranchProxy, 0); // branch proxy with a name
   };
}
}

#endif // defined TTreeReaderUtils
