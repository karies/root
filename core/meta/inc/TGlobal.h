// @(#)root/meta:$Id$
// Author: Rene Brun   13/11/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TGlobal
#define ROOT_TGlobal


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TGlobal                                                              //
//                                                                      //
// Global variables class (global variables are obtained from CINT).    //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#ifndef ROOT_TDictionary
#include "TDictionary.h"
#endif


class TGlobal : public TDictionary {

private:
   DataMemberInfo_t  *fInfo;      //pointer to CINT data member info

public:
   TGlobal(DataMemberInfo_t *info = 0);
   TGlobal (const TGlobal &);
   TGlobal &operator=(const TGlobal &);
   
   virtual       ~TGlobal();
   virtual Int_t  GetArrayDim() const;
   virtual DeclId_t GetDeclId() const;
   virtual Int_t  GetMaxIndex(Int_t dim) const;
   virtual void  *GetAddress() const;
   virtual const char *GetTypeName() const;
   virtual const char *GetFullTypeName() const;
   virtual Bool_t IsValid();
   virtual Long_t Property() const;
   virtual bool   Update(DataMemberInfo_t *info);

   ClassDef(TGlobal,0)  //Global variable class
};

   class TGlobalMappedFunction: public TGlobal {
   public:
      typedef void* (*GlobalFunc_t)();
      TGlobalMappedFunction(const char* name, const char* type,
                            GlobalFunc_t funcPtr):
         fFuncPtr(funcPtr)
      { SetNameTitle(name, type); }
      virtual       ~TGlobalMappedFunction() {}
      Int_t          GetArrayDim() const { return 0;}
      DeclId_t       GetDeclId() const { return 0; }
      Int_t          GetMaxIndex(Int_t /*dim*/) const { return -1; }
      void          *GetAddress() const { return (*fFuncPtr)(); }
      const char    *GetTypeName() const { return fTitle; }
      const char    *GetFullTypeName() const { return fTitle; }
      Long_t         Property() const { return 0; }
      virtual bool   Update(DataMemberInfo_t * /*info*/) { return false; }

   private:
      GlobalFunc_t fFuncPtr; // Function to call to get the address

      TGlobalMappedFunction &operator=(const TGlobal &); // not implemented.
   };


#endif
