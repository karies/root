/// \file TPad.cxx
/// \ingroup Gpad ROOT7
/// \author Axel Naumann <axel@cern.ch>
/// \date 2017-07-07
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2017, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "ROOT/TPad.hxx"

#include "ROOT/TLogger.hxx"


ROOT::Experimental::Internal::TPadBase::~TPadBase() = default;

std::vector<std::vector<ROOT::Experimental::TPad*>>
ROOT::Experimental::Internal::TPadBase::Divide(int nHoriz, int nVert, const TPadPos& padding /*= {}*/) {
   std::vector<std::vector<TPad*>> ret;
   if (!nHoriz)
      R__ERROR_HERE("Gpad") << "Cannot divide into 0 horizontal sub-pads!";
   if (!nVert)
      R__ERROR_HERE("Gpad") << "Cannot divide into 0 vertical sub-pads!";
   if (!nHoriz || !nVert)
      return ret;

   // Start with the whole (sub-)pad:
   TPadPos offset{1._normal, 1._normal};
   /// We need n Pads plus n-1 padding. Thus each `(subPadSize + padding)` is `(parentPadSize + padding) / n`.
   offset = (offset + padding);
   offset *= {1./nHoriz, 1./nVert};
   const TPadPos size = offset - padding;

   ret.resize(nHoriz);
   for (int iHoriz = 0; iHoriz < nHoriz; ++iHoriz) {
      ret[iHoriz].resize(nVert);
      for (int iVert = 0; iVert < nVert; ++iVert) {
         TPadPos subPos = offset;
         subPos *= {1. * nHoriz, 1. * nVert};
         ret[iHoriz][iVert] = Draw(std::make_unique<TPad>(*this, size), subPos).Get();
      }
   }
   return ret;
}

ROOT::Experimental::TPad::~TPad() = default;

const TPadCoord PixelsToNormal(const TPadCoord &pos) const {
   if (auto canv = dynamic_cast<const TCanvas*>(fParent)) {
      TPadCoord::Pixels canvSize = canv->GetSize();
      return {pos.fPixels[0] / fSize.fHoriz.fPixel, pos[1] / fSize.fVert.fPixel};
   }
// Normalized coords given the parent size:
std::array<TPadCoord::Normal, 2> parentNormal = fParent->ToNormal({pos[0], pos[1]});

// Our size is fSize; need to know in parent's Normal:
Normal mySizeInParentNormal = ;
return {parentNormal[0] / fParent->ToNormal(fSize).}
}
