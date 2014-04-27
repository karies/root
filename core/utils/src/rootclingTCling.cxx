// @(#)root/utils:$Id$
// Author: Axel Naumann, 2014-04-07

/*************************************************************************
 * Copyright (C) 1995-2014, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

// Provides bindings to TCling (compiled with rtti) from rootcling (compiled
// without rtti).

#include "TCling.h"
#include "TROOT.h"
#include "TFile.h"
#include "TClass.h"
#include "TStreamerInfo.h"

std::string gPCMFilename;
TObjArray gStreamerInfos;

extern "C"
cling::Interpreter* TCling__GetInterpreter()
{
   static bool sInitialized = false;
   gROOT; // trigger initialization
   if (!sInitialized) {
      gCling->SetClassAutoloading(false);
      sInitialized = true;
   }
   return ((TCling*)gCling)->GetInterpreter();
}

extern "C"
void InitializeStreamerInfoROOTFile(const char* filename)
{
   gPCMFilename = filename;
   TVirtualStreamerInfo::SetFactory(new TStreamerInfo());
}

extern "C"
void CloseStreamerInfoROOTFile()
{
   // Don't use TFile::Open(); we don't need plugins.
   TFile dictFile(gPCMFilename.c_str(), "RECREATE");
   // Instead of plugins:
   gStreamerInfos.Write("__StreamerInfoOffsets", TObject::kSingleKey);
}

extern "C"
bool AddStreamerInfoToROOTFile(const char* normName)
{
   TClass* cl = TClass::GetClass(normName, kTRUE /*load*/);
   if (!cl)
      return false;
   // If the class is not persistent we return success.
   Version_t classVersion = cl->GetClassVersion();
   if (classVersion == 0)
      return true;
   TStreamerInfo* SI = new TStreamerInfo(cl);
   // Must register the SI "temporarily" in the ListOfStreamerInfos
   // for the resolution of counter elements:
   TObjArray *sinfos = const_cast<TObjArray*>(cl->GetStreamerInfos());
   sinfos->AddAtAndExpand(SI, classVersion);
   SI->Build(true /*local*/);
   SI->BuildOffsets();
   gStreamerInfos.AddLast(SI);
   return true;
}
