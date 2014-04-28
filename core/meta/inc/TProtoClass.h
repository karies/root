// @(#)root/meta:$Id$
// Author: Axel Naumann 2014-04-28

/*************************************************************************
 * Copyright (C) 1995-2014, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/


#ifndef ROOT_TProtoClass
#define ROOT_TProtoClass

#ifndef ROOT_TNamed
#include "TNamed.h"
#endif

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TProtoClass                                                          //
//                                                                      //
// Stores enough data to create a TClass from a dictionary.             //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

class TProtoClass: public TNamed {
public:
   TList              *fRealData; // TRealData for members + bases
   TList              *fBase;     // List of base classes
   TListOfDataMembers *fData;     // List of data members
   UInt_t              fCheckSum; // Checksum of data members and base classes
   Int_t               fSizeof;   // Size of the class
   Int_t               fCanSplit; // Whether this class can be split
   Long_t              fProperty; // Class properties, see EProperties

   TProtoClass(const TProtoClass&) = delete;
   TProtoClass& operator=(const TProtoClass&) = delete;

   TProtoClass() {}
   virtual ~TProtoClass();

   ClassDef(TProtoClass,2); //Persistent TClass
};

#endif
